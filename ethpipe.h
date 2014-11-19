#ifndef _ETHPIPE_H_
#define _ETHPIPE_H_

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/pci.h>

#define VERSION  "0.4.0"
#define DRV_NAME "ethpipe"

#define EP_MAGIC           0x3776
#define EP_HDR_SIZE        12       // magic:2 + frame_len:2 + ts:8
#define EP_HWHDR_SIZE      14       // frame_len:2 + hash:4 + ts:8
#define MAX_PKT_SIZE       9014
#define MIN_PKT_SIZE       40
#define RING_ALMOST_FULL   (MAX_PKT_SIZE*2)
#define XMIT_BUDGET        0x3F

/* NIC parameters */
#define TX0_WRITE_ADDR          0x30
#define TX0_READ_ADDR           0x34
#define NUM_TX_TIMESTAMP_REG    2
#define DMA_BUF_MAX             (1024*1024)


#define func_enter() pr_debug("entering %s\n", __func__);

#define RING_INFO(X)                          \
printk("[%s]: size=%d, ", __func__, X->size); \
printk("mask=%d, ", X->mask);                 \
printk("start=%p, ", X->start);               \
printk("end=%p, ", X->end);                   \
printk("read=%p, ", X->read);                 \
printk("write=%p\n", X->write);


struct ep_thread {
	unsigned int cpu;         /* cpu id that the thread is runnig */
	struct task_struct *tsk;  /* xmit kthread */
};

struct ep_ring {
	uint32_t size;            /* malloc size of ring */
	uint8_t *start;           /* start address */
	uint8_t *end;             /* end address */
	uint32_t mask;            /* (size - 1) of ring */
	volatile uint8_t *read;   /* next position to be read */
	volatile uint8_t *write;  /* next position to be written */
};

/*
struct ep_timestamp_hdr {
	bool reset;
	uint8_t reg;
	struct val {
		uint16_t high;
		uint32_t low;
	} val;
};
*/

struct ep_hw_pkt {
	uint16_t len;       /* frame length */
	uint32_t hash;      /* unused. reserved */
	uint64_t ts;        /* timestamp */
	uint8_t body[1];    /* ethnet frame data */
} __attribute__((__packed__));

struct mmio {
	uint8_t *virt;
	uint64_t start;
	uint64_t end;
	uint64_t flags;
	uint64_t len;
};

struct ecp3versa {
	struct pci_dev *pcidev;
	struct mmio mmio0;
	struct mmio mmio1;
	struct tx {
		volatile uint32_t *write;
		volatile uint32_t *read;
		uint32_t *start;
		uint32_t *end;
		uint32_t size;
		uint32_t mask;
	} tx;
};

struct ep_dev {
	int txq_size;          /* TX ring size */
	int rxq_size;          /* RX ring size */
	int wrq_size;          /* write ring size */
	int rdq_size;          /* read ring size */

	struct ep_ring txq;    /* tx ring buffer */
	struct ep_ring rxq;    /* rx ring buffer */
	struct ep_ring wrq;    /* to store copy_from_user() data */
	struct ep_ring rdq;    /* rx ring buffer from dev_add_pack */

	struct ep_thread txth; /* tx thread for sending packets */
//	struct ep_thread rxth; /* rx thread for recv packets */

	uint32_t tx_counter;   /* tx packet counter */
	uint32_t rx_counter;   /* rx packet counter */

	/* RX wait queue */
	wait_queue_head_t read_q;
	struct semaphore pktdev_sem;

	/* temporary buffer for build packet */
	struct ep_hw_pkt *hw_pkt;

	/* NIC */
	struct ecp3versa nic;
};

/* Global variables */
static struct ep_dev *pdev;

/* Module parameters, defaults. */
static int debug = 0;
static int txq_size = 32;
static int rxq_size = 32;
static int wrq_size = 32;
static int rdq_size = 32;


static inline uint32_t ring_count(const struct ep_ring *r)
{
	return ((r->write - r->read) & r->mask);
}

static inline uint32_t ring_free_count(const struct ep_ring *r)
{
	return ((r->read - r->write - 1) & r->mask);
}

static inline bool ring_empty(const struct ep_ring *r)
{
	return !!(r->read == r->write);
}

static inline bool ring_almost_full(const struct ep_ring *r)
{
	return !!(ring_free_count(r) < RING_ALMOST_FULL);
}

static inline uint16_t ring_next_magic(struct ep_ring *r)
{
	return *(uint16_t *)&r->read[0];
}

static inline uint16_t ring_next_frame_len(struct ep_ring *r)
{
	return *(uint16_t *)&r->read[2];
}

static inline uint64_t ring_next_timestamp(struct ep_ring *r)
{
	return *(uint64_t *)&r->read[4];
}

static inline uint8_t ring_next_ts_reset(struct ep_ring *r)
{
	return r->read[4];
}

static inline uint8_t ring_next_ts_reg(struct ep_ring *r)
{
	return r->read[5];
}

static inline void ring_write_next(struct ep_ring *r, uint32_t size)
{
	r->write += size;
	if (r->write > r->end) {
		r->write = r->start;
	}
}

static inline void ring_read_next(struct ep_ring *r, uint32_t size)
{
	r->read += size;
	if (r->read > r->end) {
		r->read = r->start;
	}
}

static inline void ring_write_next_aligned(struct ep_ring *r, uint32_t size)
{
	r->write += ALIGN(size, 4);
	if (r->write > r->end) {
		r->write = r->start;
	}
}

static inline void ring_read_next_aligned(struct ep_ring *r, uint32_t size)
{
	r->read += ALIGN(size, 4);
	if (r->read > r->end) {
		r->read = r->start;
	}
}

static inline uint32_t hwtx_xmit_next(uint32_t hw_write, uint32_t size)
{
	struct ecp3versa *nic = &pdev->nic;

	hw_write += ALIGN(EP_HWHDR_SIZE + size, 2);
	hw_write &= nic->tx.mask;

	return hw_write;
}

static inline uint32_t read_nic_txptr(uint32_t *p)
{
	return *p << 1;
}

static inline void set_nic_txptr(uint32_t *p, uint32_t addr)
{
	*p = addr >> 1;
}

static inline void dump_nic_info(struct ep_hw_pkt *hw_pkt)
{
	struct ecp3versa *nic = &pdev->nic;

	pr_info("nic->tx.write: %p, %X\n", nic->tx.write, *nic->tx.write);
	pr_info("nic->tx.read: %p, %X\n", nic->tx.read, *nic->tx.read);
	pr_info("nic->tx.end: %p\n", nic->tx.end);
	pr_info("nic->tx.size: %X\n", (unsigned int)nic->tx.size);

	pr_info("----packet\n");
	pr_info("hw_pkt->len: %X\n", (uint32_t)hw_pkt->len);
	pr_info("hw_pkt->hash: %X\n", (uint32_t)hw_pkt->hash);
	pr_info("hw_pkt->ts: %X\n", (uint32_t)hw_pkt->ts);
	pr_info("hw_pkt->body0: %02X%02X%02X%02X%02X%02X\n",
		hw_pkt->body[ 0], hw_pkt->body[ 1], hw_pkt->body[ 2],
		hw_pkt->body[ 3], hw_pkt->body[ 4], hw_pkt->body[ 5]);
	pr_info("hw_pkt->body1: %02X%02X%02X%02X%02X%02X\n",
		hw_pkt->body[ 6], hw_pkt->body[ 7], hw_pkt->body[ 8],
		hw_pkt->body[ 9], hw_pkt->body[10], hw_pkt->body[11]);
	pr_info("hw_pkt->body2: %02X%02X\n",
		hw_pkt->body[12], hw_pkt->body[13]);
	pr_info("----packet\n");
}

#endif /* _ETHPIPE_H_ */



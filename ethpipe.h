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
	volatile uint32_t *tx_write;
	volatile uint32_t *tx_read;
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
	return (r->read[0] << 8) | r->read[1];
}

static inline uint16_t ring_next_frame_len(struct ep_ring *r)
{
	return (r->read[2] << 8) | r->read[3];
}

static inline bool ring_next_ts_reset(struct ep_ring *r)
{
	return r->read[4];
}

static inline uint8_t ring_next_ts_reg(struct ep_ring *r)
{
	return r->read[5];
}

/*
static inline uint16_t ring_next_ts_val_high(struct ep_ring *r)
{
	return (r->read[6] << 8) | r->read[7];
}

static inline uint32_t ring_next_ts_val_low(struct ep_ring *r)
{
	return (r->read[ 8] << 24) | (r->read[9] << 16)
	     | (r->read[10] <<  8) | r->read[11]
}
*/

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

static inline void pcie_pio_set(uint32_t *p, uint32_t n)
{
	*p = ALIGN(n, 2) >> 1;
}

static inline uint32_t pcie_pio_read(uint32_t *p, uint32_t n)
{
	return ALIGN(n, 2) << 1;
}
#endif /* _ETHPIPE_H_ */



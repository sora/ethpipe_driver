#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/smp.h>

#define VERSION  "0.4.0"
#define DRV_NAME "ethpipe"

#define EP_MAGIC           (0x3776)
#define EP_HDR_SIZE        (4)
#define MAX_PKT_SIZE       (9014)
#define MIN_PKT_SIZE       (40)
#define EP_BUF_SIZE        (1024*1024*4)
#define RING_ALMOST_FULL   (MAX_PKT_SIZE*3)
#define XMIT_BUDGET        (0x3F)

/* EtherPIPE NIC IOMMU registers */
#define TX0_WRITE_ADDR     (0x30)
#define TX0_READ_ADDR      (0x34)


#define func_enter() pr_debug("entering %s\n", __func__);

#define RING_INFO(X)                         \
printk("[%s]: size=%d, ", __func__, X->size); \
printk("mask=%d, ", X->mask);                 \
printk("start=%p, ", X->start);               \
printk("end=%p, ", X->end);                   \
printk("read=%p, ", X->read);                 \
printk("write=%p\n", X->write);


struct ep_thread {
	unsigned int cpu;			/* cpu id that the thread is runnig */
	struct task_struct *tsk;		/* xmit kthread */
};

struct ep_ring {
	uint32_t size;						/* size of ring */
	uint32_t mask;						/* (size - 1) of ring */
	uint8_t *start;					/* start pointer of buffer */
	uint8_t *end;						/* end pointer of buffer */
	uint8_t *read;			/* next position to be read */
	uint8_t *write;		/* next position to be written */
};

struct ep_dev {
	/* TX ring size */
	int txq_size;

	/* RX ring size */
	int rxq_size;

	/* RX wait queue */
	wait_queue_head_t read_q;
	struct semaphore pktdev_sem;

	/* tx thread for sending packets */
	struct ep_thread txth;

	/* rx thread for recv packets */
//	struct ep_thread rxth;

	/* tx ring buffer */
	struct ep_ring txq;

	/* rx ring buffer */
	struct ep_ring rxq;

	/* tx tmp buffer to store copy_from_user() data */
	struct ep_ring wrq;

	/* rx ring buffer from dev_add_pack */
	struct ep_ring rdq;
};

/* Global variables */
static struct ep_dev *pdev;

/* Module parameters, defaults. */
static int debug = 0;
static int txq_size = 1;
static int rxq_size = 1;


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

static inline uint32_t ring_next_magic(struct ep_ring *r)
{
	return (r->read[0] << 8) | r->read[1];
}

static inline uint32_t ring_next_frame_len(struct ep_ring *r)
{
	return (r->read[2] << 8) | r->read[3];
}

static inline void ring_write_next(struct ep_ring *r, uint32_t size)
{
	if (r->write < r->end) {
		r->write += size;
	} else {
		r->write = r->start;
	}
}

static inline void ring_write_next_aligned(struct ep_ring *r, uint32_t size)
{
	if (r->write < r->end) {
		r->write += ALIGN(size, 4);
	} else {
		r->write = r->start;
	}
}

static inline void ring_read_next(struct ep_ring *r, uint32_t size)
{
	r->read += size;
	if (r->read > r->end)
		r->read = r->start;
}

static inline void ring_read_next_aligned(struct ep_ring *r, uint32_t size)
{
	r->read += ALIGN(size, 4);
	if (r->read > r->end)
		r->read = r->start;
}

static inline void pcie_pio_set(uint32_t *p, uint32_t n)
{
	*p = ALIGN(n, 2) >> 1;
}


static inline uint32_t pcie_pio_read(uint32_t *p, uint32_t n)
{
	return ALIGN(n, 2) << 1;
}


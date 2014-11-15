#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <linux/pci.h>
#include "ethpipe.h"

static int ethpipe_open(struct inode *inode, struct file *filp);
static int ethpipe_release(struct inode *inode, struct file *filp);
static ssize_t ethpipe_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos);
static ssize_t ethpipe_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *ppos);
static unsigned int ethpipe_poll( struct file* filp, poll_table* wait );
static long ethpipe_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg);

static void ethpipe_send_dummy(void);
static int ethpipe_tx_kthread(void *unused);
static int ethpipe_pdev_init(void);
static void ethpipe_pdev_free(void);

static int ethpipe_nic_init(struct pci_dev *pcidev,
		const struct pci_device_id *ent);
static void ethpipe_nic_remove(struct pci_dev *pcidev);


static struct file_operations ethpipe_fops = {
	.owner = THIS_MODULE,
	.read = ethpipe_read,
	.write = ethpipe_write,
	.poll = ethpipe_poll,
	.compat_ioctl = ethpipe_ioctl,
	.open = ethpipe_open,
	.release = ethpipe_release,
};

static struct miscdevice ethpipe_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRV_NAME,
	.fops = &ethpipe_fops,
};

DEFINE_PCI_DEVICE_TABLE(ethpipe_pci_tbl) = {
	{0x3776, 0x8001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{0,}
};
MODULE_DEVICE_TABLE(pci, ethpipe_pci_tbl);

struct pci_driver ethpipe_pci_driver = {
	.name = DRV_NAME,
	.id_table = ethpipe_pci_tbl,
	.probe = ethpipe_nic_init,
	.remove = ethpipe_nic_remove,
//	.suspend = ethpipe_suspend,
//	.resume = ethpipe_resume,
};


/*
 * ethpipe_open
 */
static int ethpipe_open(struct inode *inode, struct file *filp)
{
	func_enter();

	return 0;
}

/*
 * ethpipe_release
 */
static int ethpipe_release(struct inode *inode, struct file *filp)
{
	func_enter();

	return 0;
}

/*
 * ethpipe_recv
 */
static void ethpipe_recv(void)
{
	func_enter();

	return;
}

/*
 * ethpipe_read
 */
static ssize_t ethpipe_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *ppos)
{
	func_enter();

	ethpipe_recv();

	return count;
}


/*
 * ethpipe_send_dummy
 */
static void ethpipe_send_dummy(void)
{
	int limit;
	uint16_t magic, frame_len;
	struct ep_ring *txq = &pdev->txq;

	func_enter();

	// reset xmit budget
	limit = XMIT_BUDGET;

	// sending
	while(!ring_empty(txq) && (--limit > 0)) {
		// check magic code
		magic = ring_next_magic(txq);
		if (magic != EP_MAGIC) {
			pr_info("packet format error: magic=%X\n", (int)magic);
			goto error;
		}

		// check frame length
		frame_len = ring_next_frame_len(txq);
		if ((frame_len > MAX_PKT_SIZE) || (frame_len < MIN_PKT_SIZE)) {
			pr_info("packet format error: frame_len=%X\n", (int)frame_len);
			goto error;
		}

#if 0
		int i;
		printk("PKT:");
		printk(" %d", magic);
		printk(" %d", frame_len);
		for (i = 0; i < frame_len; i++) {
			printk(" %02X", txq->read[EP_HDR_SIZE+i]);
		}
		printk("\n");
#endif

		ring_read_next_aligned(txq, EP_HDR_SIZE + frame_len);

		// incr tx_counter
		++pdev->tx_counter;
	}

	return;

error:
	pr_info("kthread: tx_err\n");
	txq->read = txq->start;
	txq->write = txq->start;

	return;
}

/*
 * ethpipe_write
 */
static ssize_t ethpipe_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	uint16_t magic, frame_len, len;
	uint8_t ts_reg;
	bool ts_reset;
	struct ep_ring *wrq = &pdev->wrq;
	struct ep_ring *txq = &pdev->txq;

	func_enter();

	// check count :todo

	// reset wrq
	wrq->write = wrq->start;
	wrq->read = wrq->start;

	// userland to wrq
	if (copy_from_user((uint8_t *)wrq->write, buf, count)) {
		pr_info("copy_from_user failed. \n");
		RING_INFO(wrq);
		RING_INFO(txq);
		return -EFAULT;
	}

	// Don't need check the buffer status of wrq.
	// because wrq is always reset entering ethpipe_write.
	ring_write_next(wrq, count);

	// wrq to txq
	while (!ring_empty(wrq)) {
		// check magic code
		magic = ring_next_magic(wrq);
		if (magic != EP_MAGIC) {
			pr_info("packet format error: magic=%X\n", (int)magic);
			return -EFAULT;
		}

		// check frame length
		frame_len = ring_next_frame_len(wrq);
		if ((frame_len > MAX_PKT_SIZE) || (frame_len < MIN_PKT_SIZE)) {
			pr_info("packet format error: frame_len=%X\n", (int)frame_len);
			return -EFAULT;
		}

		// check timestamp
		ts_reset = ring_next_ts_reset(wrq);
		if (ts_reset > 1) {
			pr_info("packet format error: ts_reset=%X\n", (int)ts_reset);
			return -EFAULT;
		}
		ts_reg = ring_next_ts_reg(wrq);
		if (ts_reg >= NUM_TX_TIMESTAMP_REG) {
			pr_info("packet format error: ts_reg=%X\n", (int)ts_reset);
			return -EFAULT;
		}

		// memcpy
		len = EP_HDR_SIZE + frame_len;
		if (!ring_almost_full(txq)) {
			memcpy((uint8_t *)txq->write, (uint8_t *)wrq->read, len);
			ring_read_next(wrq, len);
			ring_write_next_aligned(txq, len);
		} else {
			// return when a ring buffer reached the max size
			pr_debug("txq is full.\n");
			cpu_relax();
			break;
		}
	}

	return (count - ring_count(wrq));
}

/*
 * ethpipe_poll
 */
static unsigned int ethpipe_poll(struct file* filp, poll_table* wait)
{
	unsigned int retmask = 0;

	func_enter();

	poll_wait(filp, &pdev->read_q, wait);

//	if (pdev->rxbuf.read_ptr != pdev->rxbuf.write_ptr) {
//		retmask |= (POLLIN  | POLLRDNORM);
//	}

	return retmask;
}

/*
 * ethpipe_ioctl
 */
static long ethpipe_ioctl(struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	func_enter();

	return  -ENOTTY;
}


static int ethpipe_tx_kthread(void *unused)
{
	int cpu = smp_processor_id();

	pr_info("starting ethpiped/%d:  pid=%d\n", cpu, task_pid_nr(current));

	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {
		//pr_info("[kthread] my cpu is %d (%d, HZ=%d)\n", cpu, i++, HZ);

		if (pdev->txq.read == pdev->txq.write) {
			schedule_timeout_interruptible(1);
			continue;
		}

		__set_current_state(TASK_RUNNING);

		ethpipe_send_dummy();
		if (need_resched())
			schedule();
		else
			cpu_relax();

		set_current_state(TASK_INTERRUPTIBLE);
	}

	pr_info("kthread_exit: cpu=%d\n", cpu);

	return 0;
}

static void ethpipe_pdev_free(void)
{
	pr_info("%s\n", __func__);

	kthread_stop(pdev->txth.tsk);

	/* free tx buffer */
	if (pdev->txq.start) {
		vfree(pdev->txq.start);
		pdev->txq.start = NULL;
	}

	/* free write buffers */
	if (pdev->wrq.start) {
		vfree(pdev->wrq.start);
		pdev->wrq.start = NULL;
	}

	/* free rx buffer */
	if (pdev->rxq.start) {
		vfree(pdev->rxq.start);
		pdev->rxq.start = NULL;
	}

	/* free read buffers */
	if (pdev->rdq.start) {
		vfree(pdev->rdq.start);
		pdev->rdq.start = NULL;
	}

	if (pdev) {
		kfree(pdev);
		pdev = NULL;
	}
}

static int ethpipe_pdev_init(void)
{
	pr_info("%s\n", __func__);

	/* malloc pdev */
	if ((pdev = kmalloc(sizeof(struct ep_dev), GFP_KERNEL)) == 0) {
		pr_info("fail to kmalloc: *pdev\n");
		goto err;
	}

	pdev->tx_counter = 0;
	pdev->rx_counter = 0;

	/* tx ring size from module parameter */
	pdev->txq_size = txq_size * 1024 * 1024;
	pr_info("pdev->txq_size: %d\n", pdev->txq_size);

	/* rx ring size from module parameter */
	pdev->rxq_size = rxq_size * 1024 * 1024;
	pr_info("pdev->rxq_size: %d\n", pdev->rxq_size);

	/* write ring size from module parameter */
	pdev->wrq_size = wrq_size * 1024 * 1024;
	pr_info("pdev->wrq_size: %d\n", pdev->wrq_size);

	/* read ring size from module parameter */
	pdev->rdq_size = rdq_size * 1024 * 1024;
	pr_info("pdev->rdq_size: %d\n", pdev->rdq_size);

	/* setup transmit buffer */
	if ((pdev->txq.start =
			vmalloc(pdev->txq_size + EP_HDR_SIZE + MAX_PKT_SIZE)) == 0) {
		pr_info("fail to vmalloc: txq\n");
		goto err;
	}
	pdev->txq.size  = pdev->txq_size;
	pdev->txq.mask  = pdev->txq_size - 1;
	pdev->txq.end   = pdev->txq.start + pdev->txq_size - 1;
	pdev->txq.write = pdev->txq.start;
	pdev->txq.read  = pdev->txq.start;

	/* setup receive buffer */
	if ((pdev->rxq.start =
			vmalloc(pdev->rxq_size + EP_HDR_SIZE + MAX_PKT_SIZE)) == 0) {
		pr_info("fail to vmalloc: rxq\n");
		goto err;
	}
	pdev->rxq.size  = pdev->rxq_size;
	pdev->rxq.mask  = pdev->rxq_size - 1;
	pdev->rxq.end   = pdev->rxq.start + pdev->rxq_size - 1;
	pdev->rxq.write = pdev->rxq.start;
	pdev->rxq.read  = pdev->rxq.start;

	/* setup write buffer */
	if ((pdev->wrq.start =
			vmalloc(pdev->wrq_size + EP_HDR_SIZE + MAX_PKT_SIZE)) == 0) {
		pr_info("fail to vmalloc: wrq\n");
		goto err;
	}
	pdev->wrq.size  = pdev->wrq_size;
	pdev->wrq.mask  = pdev->wrq_size - 1;
	pdev->wrq.end   = pdev->wrq.start + pdev->wrq_size - 1;
	pdev->wrq.write = pdev->wrq.start;
	pdev->wrq.read  = pdev->wrq.start;

	/* setup read buffer */
	if ((pdev->rdq.start =
			vmalloc(pdev->rdq_size + EP_HDR_SIZE + MAX_PKT_SIZE)) == 0) {
		pr_info("fail to vmalloc: rdq\n");
		goto err;
	}
	pdev->rdq.size  = pdev->rdq_size;
	pdev->rdq.mask  = pdev->rdq_size - 1;
	pdev->rdq.end   = pdev->rdq.start + pdev->rdq_size - 1;
	pdev->rdq.write = pdev->rdq.start;
	pdev->rdq.read  = pdev->rdq.start;

	// create tx thread
	pdev->txth.tsk = kthread_run(ethpipe_tx_kthread, NULL, "tx_kthread");
	if (IS_ERR(pdev->txth.tsk)) {
		pr_info("can't create tx thread\n");
		goto err;
	}

	return 0;

err:
	return -1;
}

/*
 * ethpipe_nic_init()
 */
static int ethpipe_nic_init(struct pci_dev *pcidev,
		const struct pci_device_id *ent)
{
	int rc;
	struct mmio *mmio0 = &pdev->nic.mmio0;
	struct mmio *mmio1 = &pdev->nic.mmio1;
	struct ecp3versa *nic = &pdev->nic;

	pr_info("%s\n", __func__);

	rc = pci_enable_device(pcidev);
	if (rc)
		goto error;

	rc = pci_request_regions(pcidev, DRV_NAME);
	if (rc)
		goto error;

	/* set BUS master */
	pci_set_master(pcidev);

	/* mmio0 (pcie pio) */
	mmio0->start = pci_resource_start(pcidev, 0);
	mmio0->end = pci_resource_end(pcidev, 0);
	mmio0->flags = pci_resource_flags(pcidev, 0);
	mmio0->len = pci_resource_len(pcidev, 0);
	mmio0->virt = ioremap(mmio0->start, mmio0->len);
	if(!mmio0->virt) {
		pr_info("cannot ioremap MMIO0 base\n");
		goto error;
	}
	pr_info("mmio0_start: %X\n", (unsigned int)mmio0->start);
	pr_info("mmio0_end  : %X\n", (unsigned int)mmio0->end);
	pr_info("mmio0_flags: %X\n", (unsigned int)mmio0->flags);
	pr_info("mmio0_len  : %X\n", (unsigned int)mmio0->len);

	/* mmio1 (pcie pio + write combining) */
	mmio1->start = pci_resource_start(pcidev, 2);
	mmio1->end = pci_resource_end(pcidev, 2);
	mmio1->flags = pci_resource_flags(pcidev, 2);
	mmio1->len = pci_resource_len(pcidev, 2);
	mmio1->virt = ioremap_wc(mmio1->start, mmio1->len);
	if (!mmio1->virt) {
		pr_info("cannot ioremap MMIO1 base\n");
		goto error;
	}
	pr_info("mmio1_start: %X\n", (unsigned int)mmio1->start);
	pr_info("mmio1_end  : %X\n", (unsigned int)mmio1->end);
	pr_info("mmio1_flags: %X\n", (unsigned int)mmio1->flags);
	pr_info("mmio1_len  : %X\n", (unsigned int)mmio1->len);


	/* initial NIC hardware registers */
	*(long     *)(mmio0->virt + 0x14) = DMA_BUF_MAX; /* set DMA Buffer length */
	*(uint32_t *)(mmio0->virt + 0x30) = 0;
	*(uint32_t *)(mmio1->virt + 0x34) = 0;
	*(long     *)(mmio0->virt + 0x80) = 1; /* set min disable interrupt cycles (@125MHz) */
	*(long     *)(mmio0->virt + 0x84) = 0xffffffff; /* set max enable interrupt cycles (@125MHz) */

	/* pointer of NIC registers */
	nic->tx_write = (uint32_t *)(mmio0->virt + 0x30);
	nic->tx_read = (uint32_t *)(mmio0->virt + 0x34);

	return 0;

error:
	pci_release_regions(pcidev);
	pci_disable_device(pcidev);
	return -1;
}

/*
 * ethpipe_nic_remove()
 */
static void ethpipe_nic_remove(struct pci_dev *pcidev)
{
	struct mmio *mmio0 = &pdev->nic.mmio0;
	struct mmio *mmio1 = &pdev->nic.mmio1;

	pr_info("%s\n", __func__);

	*(uint32_t *)(mmio0->virt + 0x30) = 0;
	*(uint32_t *)(mmio1->virt + 0x34) = 0;

	if (mmio0->virt) {
		iounmap(mmio0->virt);
		mmio0->virt = 0;
	}
	if (mmio1->virt) {
		iounmap(mmio1->virt);
		mmio1->virt = 0;
	}

	ethpipe_pdev_free();

	pci_release_regions(pcidev);
	pci_disable_device(pcidev);
}

/*
 * ethpipe_init()
 */
static int __init ethpipe_init(void)
{
	int ret = 0, idx = 0;
	static char name[16];

	pr_info("%s\n", __func__);

	/* register character device */
	sprintf(name, "%s/%d", DRV_NAME, idx);
	ethpipe_dev.name = name;
	ret = misc_register(&ethpipe_dev);
	if (ret) {
		pr_info("fail to misc_register (MISC_DYNAMIC_MINOR)\n");
		goto error;
	}

	ret = ethpipe_pdev_init();
	if (ret < 0)
		goto error;

	return pci_register_driver(&ethpipe_pci_driver);

error:
	pci_unregister_driver(&ethpipe_pci_driver);
	return -1;
}

/*
 * ethpipe_cleanup
 */
static void __exit ethpipe_cleanup(void)
{
	pr_info("%s\n", __func__);

	misc_deregister(&ethpipe_dev);
	pci_unregister_driver(&ethpipe_pci_driver);
}


module_init(ethpipe_init);
module_exit(ethpipe_cleanup);

MODULE_AUTHOR("Yohei Kuga <sora@haeena.net>");
MODULE_DESCRIPTION("Packet Character device");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debug mode");
module_param(txq_size, int, S_IRUGO);
MODULE_PARM_DESC(txq_size, "TX ring size on each xmit kthread (MB)");
module_param(rxq_size, int, S_IRUGO);
MODULE_PARM_DESC(rxq_size, "RX ring size on each recv kthread (MB)");
module_param(wrq_size, int, S_IRUGO);
MODULE_PARM_DESC(wrq_size, "Write ring size on ep_write (dMB)");
module_param(rdq_size, int, S_IRUGO);
MODULE_PARM_DESC(rdq_size, "Read ring size on ep_read (MB)");


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/stat.h>
#include <linux/seq_file.h>

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#include <linux/string.h>
#include <linux/version.h>


#define VERSION "0.3.0"
#define DRV_NAME "ethpipe"

/* PCIe MMIO_0 Mapped Memory Address Table */
#define TX_WR_PTR_ADDR	(0x30)
#define TX_RD_PTR_ADDR	(0x34)

/* buffer size */
#define RING_BUF_MAX	  (1024*1024*1)

/* support old Linux version */
#if LINUX_VERSION_CODE > KERNEL_VERSION(3,8,0)
#define __devinit
#define __devexit
#define __devexit_p
#endif

/* module parameters */
static int debug = 1;

/* pci parameters */
static DEFINE_PCI_DEVICE_TABLE(ep_pci_tbl) = {
	{ 0x3776, 0x8001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ep_pci_tbl);

/* pci registers */
static unsigned char *mmio0_ptr = 0, *mmio1_ptr = 0;    // mmio pointer
static unsigned long long mmio0_start, mmio0_end, mmio0_flags, mmio0_len;
static unsigned long long mmio1_start, mmio1_end, mmio1_flags, mmio1_len;

/* workqueue */
static struct workqueue_struct *ep_wq;
struct work_struct work1;

/* procfs */
struct proc_dir_entry *ep_proc_root;    // proc root dir

/* scheduled transmission */
static unsigned long long *counter125;
static unsigned long long *lap1, *lap2;

/* sender */
static int *sender_wr_ptr, *sender_rd_ptr;    // NIC TX pointer

/* ring buffer */
struct _pbuf_tx {
	unsigned char *tx_start_ptr;
	unsigned char *tx_end_ptr;
	unsigned char *tx_wr_ptr;
	unsigned char *tx_rd_ptr;
} static pbuf0 = {0};

/**
 * procfs handle functions
 *
 **/
static ssize_t counter125_show( struct file *file, char *buf, size_t count,
				loff_t *ppos )
{
	int len = 0;

	if (debug)
		pr_info("%s\t%d\n", __func__, (int)count);

	len = sprintf(buf, "%llX\n", *counter125);
	return len;
}

static ssize_t lap1_show( struct file *file, char *buf, size_t count,
				loff_t *ppos )
{
	int len;

	if (debug)
		pr_info("%s\n", __func__);

	len = sprintf(buf, "%llX\n", *lap1);
	return len;
}

static ssize_t lap1_store(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if (debug)
		pr_info("%s\n", __func__);

	sscanf(buf, "%llX", lap1);
	return count;
}

static ssize_t lap2_show( struct file *file, char *buf, size_t count,
				loff_t *ppos )
{
	int len;

	if (debug)
		pr_info("%s\n", __func__);

	len = sprintf(buf, "%llX\n", *lap2);
	return len;
}

static ssize_t lap2_store(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if (debug)
		pr_info("%s\n", __func__);

	sscanf(buf, "%llX", lap2);
	return count;
}

static const struct file_operations proc_counter125_fops = {
	.read = counter125_show,
};

static const struct file_operations proc_lap1_fops = {
	.read = lap1_show,
	.write = lap1_store,
};

static const struct file_operations proc_lap2_fops = {
	.read = lap2_show,
	.write = lap2_store,
};

/**
 * ep_open
 *
 **/
static int ep_open(struct inode *inode, struct file *file)
{
	if (debug)
		pr_info("%s\n", __func__);

	return 0;
}

#if 0
static const unsigned char pkt[] = {
	// ethpipe header
	0x00, 0x3c,                                      /* pktlen */
	0x00, 0x00, 0x00, 0x00,                          /* 5tuple-hash */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* timestamp */
	// ethernet header
	0x00, 0x21, 0x6a, 0x05, 0xf5, 0x2c,              /* dst mac */
	0x00, 0x24, 0x17, 0xa3, 0xf4, 0x73,              /* src mac */
	0x80, 0x00,                                      /* proto type */
	// IPv4 header
	0x45, 0x00, 0x00, 0x2e,                          /*  */
	0xdf, 0x2e, 0x00, 0x00,                          /*  */
	0x6e, 0x11, 0x80, 0xd7,                          /*  */
	0x18, 0x22, 0x12, 0xaa,                          /* src IP addr */
	0xc0, 0xa8, 0x01, 0x45,                          /* dst IP addr */
	// UDP header
	0xc4, 0x98, 0xf3, 0xb2,                          /*  */
	0x00, 0x1a, 0x83, 0xfb,                          /*  */
	// payload
	0x02, 0xca, 0x02, 0x38, 0xd0, 0x25, 0x0a, 0x2f,
	0x63, 0x3b, 0x37, 0xfc, 0x47, 0xf3, 0xd4, 0x46,
	0x3f, 0xf1
};
#endif

static const unsigned char pkt[] = {
	// ethpipe header
	0x00, 0x5c,                                      /* pktlen */
	0x00, 0x00, 0x00, 0x00,                          /* 5tuple-hash */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* timestamp */
	// ethernet header
	0xa0, 0x36, 0x9f, 0x18, 0x50, 0xe5,
	0x00, 0x1c, 0x7e, 0x6a, 0xba, 0xd1,
	0x08, 0x00,
	// IPv4 header
	0x45, 0x00, 0x00, 0x4e,
	0x00, 0x00, 0x40, 0x00,
	0x40, 0x11, 0xfb, 0x32,
	0x0a, 0x00, 0x00, 0x6e,
	0x0a, 0x00, 0x00, 0x02,
	// payload
	0x04, 0x04, 0x00, 0x89, 0x00, 0x3a, 0x38, 0x03,
	0x10, 0xfd, 0x01, 0x10, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x20, 0x46, 0x45, 0x45,
	0x4e, 0x45, 0x42, 0x46, 0x45, 0x46, 0x44, 0x46,
	0x46, 0x46, 0x4a, 0x45, 0x42, 0x43, 0x4e, 0x45,
	0x49, 0x46, 0x41, 0x43, 0x41, 0x43, 0x41, 0x43,
	0x41, 0x43, 0x41, 0x43, 0x41, 0x00, 0x00, 0x20,
	0x00, 0x01
};

static const unsigned short pktlen = sizeof(pkt) / sizeof(pkt[0]);

/**
 * ep_write
 *
 **/
static ssize_t ep_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned int copy_len = 0;
	unsigned int sender_rd_ptr_tmp = 0;
	unsigned int sender_wr_ptr_tmp = 0;
	int tx_free;
	int piece;

	if (debug)
		pr_info("%s\n", __func__);

	if (debug) {
		pr_info( "before\n" );
		pr_info( "count: %d\n", (int)count );
		pr_info( "copy_len: %d\n", copy_len );
		pr_info( "sender_rd_ptr_tmp: %d\n", sender_rd_ptr_tmp );
		pr_info( "sender_wr_ptr_tmp: %d\n", sender_wr_ptr_tmp );
		pr_info( "sender_wr_ptr: %d\n", *sender_wr_ptr );
		pr_info( "sender_rd_ptr: %d\n\n", *sender_rd_ptr );
	}

	// sender_rd_ptr_tmp
	sender_rd_ptr_tmp = *sender_rd_ptr << 1;
	sender_wr_ptr_tmp = *sender_wr_ptr << 1;

	// mmio1 free space
	if (sender_rd_ptr_tmp < sender_wr_ptr_tmp) {
		tx_free = sender_wr_ptr_tmp - sender_rd_ptr_tmp;
	} else {
		tx_free = (sender_wr_ptr_tmp - sender_rd_ptr_tmp) + (mmio1_len >> 1);
	}

	// copy_len
	if (pktlen < tx_free) {
		copy_len = pktlen;
	} else {
		copy_len = tx_free;
	}

	// Userspace to kernel TX buffer


	// copy pkt data to NIC board
	if ( (sender_wr_ptr_tmp + pktlen) < (mmio1_len >> 1) ) {
		memcpy(mmio1_ptr + sender_wr_ptr_tmp, pkt, pktlen);
	} else {
		piece = (mmio1_len >> 1) - sender_wr_ptr_tmp;
		memcpy(mmio1_ptr + sender_wr_ptr_tmp, pkt, piece);
		memcpy(mmio1_ptr, pkt + piece, pktlen - piece);
	}

	sender_wr_ptr_tmp += (pktlen + 1) & 0xfffffffe;
	sender_wr_ptr_tmp &= ((mmio1_len >> 1) - 1);

	// update sender_wr_ptr
	*sender_wr_ptr = sender_wr_ptr_tmp >> 1;

	if (debug) {
		pr_info( "after\n" );
		pr_info( "count: %d\n", (int)count);
		pr_info( "copy_len: %d\n", copy_len);
		pr_info( "sender_rd_ptr_tmp: %d\n", sender_rd_ptr_tmp);
		pr_info( "sender_wr_ptr_tmp: %d\n", sender_wr_ptr_tmp);
		pr_info( "sender_wr_ptr: %d\n", *sender_wr_ptr);
		pr_info( "sender_rd_ptr: %d\n\n", *sender_rd_ptr);
	}

	return copy_len;
}

/**
 * ep_release
 *
 **/
static int ep_release(struct inode *inode, struct file *file)
{
	if (debug)
		pr_info("%s\n", __func__);

	return 0;
}

static struct file_operations ep_fops = {
	.owner    = THIS_MODULE,
	.open     = ep_open,
	.write    = ep_write,
	.release  = ep_release,
};

static struct miscdevice ep_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DRV_NAME,
	.fops  = &ep_fops,
};

/**
 * ep_interrupt
 *
 **/
static irqreturn_t ep_interrupt(int irq, void *pdev)
{
	int status, handled = 0;

	status = *(mmio0_ptr + 0x10);

	if (debug) {
		pr_info( "Got a interrupt: status=%d\n", status );
	}

	// is ethpipe interrupt?
	if ( (status & 8) == 0 ) {
		goto lend;
	}

	handled = 1;

	// schedule workqueue
	queue_work(ep_wq, &work1);

	// clear interrupt flag
	*(mmio0_ptr + 0x10) = status & 0xf7;

lend:
	return IRQ_RETVAL(handled);
}

/**
 * ep_init_one
 *
 **/
static int __devinit ep_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	static char devname[16];
	static int board_idx = -1;
	int ret;

	pr_info( "%s\n", __func__ );

	mmio0_ptr = 0;
	mmio1_ptr = 0;

	/* attach PCI device */
	ret = pci_enable_device(pdev);
	if (ret)
		goto err;
	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret)
		goto err;

	++board_idx;
	pr_info( "board_idx: %d\n", board_idx );

	/* PCI mmio0 setup */
	mmio0_start = pci_resource_start(pdev, 0);
	mmio0_end   = pci_resource_end(pdev, 0);
	mmio0_flags = pci_resource_flags(pdev, 0);
	mmio0_len   = pci_resource_len(pdev, 0);

	pr_info( "mmio0_start: %X\n", (unsigned int)mmio0_start );
	pr_info( "mmio0_end  : %X\n", (unsigned int)mmio0_end );
	pr_info( "mmio0_flags: %X\n", (unsigned int)mmio0_flags );
	pr_info( "mmio0_len  : %X\n", (unsigned int)mmio0_len );

	mmio0_ptr = ioremap(mmio0_start, mmio0_len);
	if (!mmio0_ptr) {
		pr_warn( "cannot ioremap mmio0 base\n" );
		goto err;
	}

	/* PCI mmio1 setup */
	mmio1_start = pci_resource_start(pdev, 2);
	mmio1_end   = pci_resource_end(pdev, 2);
	mmio1_flags = pci_resource_flags(pdev, 2);
	mmio1_len   = pci_resource_len(pdev, 2);

	pr_info( "mmio1_start: %X\n", (unsigned int)mmio1_start );
	pr_info( "mmio1_end  : %X\n", (unsigned int)mmio1_end );
	pr_info( "mmio1_flags: %X\n", (unsigned int)mmio1_flags );
	pr_info( "mmio1_len  : %X\n", (unsigned int)mmio1_len );

	mmio1_ptr = ioremap_wc(mmio1_start, mmio1_len);
	if (!mmio1_ptr) {
		pr_warn( "cannot ioremap mmio1 base\n" );
		goto err;
	}

	/* register ethpipe character device */
	sprintf( devname, "%s/%d", DRV_NAME, board_idx );
	ep_misc_device.name = devname;
	ret = misc_register(&ep_misc_device);
	if (ret) {
		pr_info("Fail to misc_register (MISC_DYNAMIC_MINOR)\n");
		return ret;
	}

	/* NIC TX pointer */
	sender_wr_ptr = (int *)(mmio0_ptr + TX_WR_PTR_ADDR);
	sender_rd_ptr = (int *)(mmio0_ptr + TX_RD_PTR_ADDR);

	if (debug) {
		pr_info( "sender_wr_ptr: %X\n", *sender_wr_ptr );
		pr_info( "sender_r_ptr: %X\n", *sender_rd_ptr );
	}

	*sender_wr_ptr = *sender_rd_ptr;    // clear TX queue write pointer

	/* ring buffer */
	if ( ( pbuf0.tx_start_ptr = kmalloc(RING_BUF_MAX, GFP_KERNEL) ) == 0 ) {
		pr_err( "Fail to kmalloc: pbuf0.tx_start_ptr\n" );
		goto err;
	}
	pbuf0.tx_end_ptr = pbuf0.tx_start_ptr + RING_BUF_MAX - 1;
	pbuf0.tx_wr_ptr  = pbuf0.tx_start_ptr;
	pbuf0.tx_rd_ptr  = pbuf0.tx_start_ptr;

	/* scheduled transmission */
	counter125 = (unsigned long long *)(mmio0_ptr + 0x4);
	lap1 = (unsigned long long *)(mmio0_ptr + 0x100);
	lap2 = (unsigned long long *)(mmio0_ptr + 0x108);

	if (debug) {
		pr_info( "counter125: %X\n", *(unsigned int *)counter125 );
		pr_info( "lap1: %X\n", *(unsigned int *)lap1 );
		pr_info( "lap2: %X\n", *(unsigned int *)lap2 );
	}

	/* interrupt handler */
	if (request_irq( pdev->irq, ep_interrupt, IRQF_SHARED, DRV_NAME, pdev)) {
		pr_warn( "cannot request_irq\n" );
	}

	if (debug) {
		pr_info( "pktlen: %d\n", (int)pktlen );
	}

	return 0;

err:
	if (pbuf0.tx_start_ptr) {
		kfree(pbuf0.tx_start_ptr);
		pbuf0.tx_start_ptr = NULL;
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	return -1;
}

/**
 * ep_remove_one
 *
 **/
static void __devexit ep_remove_one(struct pci_dev *pdev)
{
	pr_info("%s\n", __func__);

	/* disable interrupts */
	disable_irq(pdev->irq);
	free_irq(pdev->irq, pdev);

	if (mmio0_ptr) {
		iounmap(mmio0_ptr);
		mmio0_ptr = 0;
	}

	if (mmio1_ptr) {
		iounmap(mmio1_ptr);
		mmio1_ptr = 0;
	}

	if (pbuf0.tx_start_ptr) {
		kfree(pbuf0.tx_start_ptr);
		pbuf0.tx_start_ptr = NULL;
	}

	/* detach pci device */
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver ep_pci_driver = {
	.name     = DRV_NAME,
	.id_table = ep_pci_tbl,
	.probe    = ep_init_one,
	.remove   = __devexit_p(ep_remove_one),
};

/**
 * work_body
 *
 **/
void work_body(struct work_struct *work)
{
	pr_info("%s\n", __func__);
	return;
}

/**
 * ep_init
 *
 **/
static int __init ep_init(void)
{
	struct proc_dir_entry *pe_counter125, *pe_lap1, *pe_lap2;
	int ret;

	pr_info("%s\n", __func__);

	/* workqueue */
	ep_wq = alloc_workqueue("ethpipe", WQ_UNBOUND, 0);
	if (!ep_wq) {
		pr_err( "alloc_workqueue failed\n" );
		ret = -ENOMEM;
		goto out;
	}
	INIT_WORK( &work1, work_body );

	/* register ethpipe procfs entries */
	// dir: /proc/ethpipe
	ep_proc_root = proc_mkdir(DRV_NAME, NULL);
	if (!ep_proc_root) {
		pr_warn("cannot create /proc/%s\n", DRV_NAME);
		return -ENODEV;
	}

	// proc file: /proc/ethpipe/counter
	pe_counter125 = proc_create("counter125", 0666, ep_proc_root,
				&proc_counter125_fops);
	if (pe_counter125 == NULL) {
		pr_err("cannot create %s procfs entry\n", "counter125");
		ret = -EINVAL;
		goto remove_counter125;
	}
	// proc file: /proc/ethpipe/lap1
	pe_lap1 = proc_create("lap1", 0666, ep_proc_root, &proc_lap1_fops);
	if (pe_lap1 == NULL) {
		pr_err("cannot create %s procfs entry\n", "lap1");
		ret = -EINVAL;
		goto remove_lap1;
	}
	// proc file: /proc/ethpipe/lap2
	pe_lap2 = proc_create("lap2", 0666, ep_proc_root, &proc_lap2_fops);
	if (pe_lap2 == NULL) {
		pr_err("cannot create %s procfs entry\n", "lap2");
		ret = -EINVAL;
		goto remove_lap2;
	}

	return pci_register_driver(&ep_pci_driver);

remove_lap2:
	remove_proc_entry("lap2", ep_proc_root);
remove_lap1:
	remove_proc_entry("lap1", ep_proc_root);
remove_counter125:
	remove_proc_entry("counter125", ep_proc_root);
	remove_proc_entry(DRV_NAME, NULL);
out:
	return ret;
}

/**
 * ep_exit_module
 *
 **/
static void __exit ep_cleanup(void)
{
	pr_info("%s\n", __func__);

	/* pci */
	misc_deregister(&ep_misc_device);
	pci_unregister_driver(&ep_pci_driver);

	/* workqueue */
	if (ep_wq) {
		flush_workqueue(ep_wq);
		destroy_workqueue(ep_wq);
		ep_wq = NULL;
	}

	/* procfs */
	remove_proc_entry("lap2", ep_proc_root);
	remove_proc_entry("lap1", ep_proc_root);
	remove_proc_entry("counter125", ep_proc_root);
	remove_proc_entry(DRV_NAME, NULL);
}

module_init(ep_init);
module_exit(ep_cleanup);

MODULE_AUTHOR("Yohei Kuga <sora@haeena.net>");
MODULE_DESCRIPTION("Ethernet Character device");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debug mode");

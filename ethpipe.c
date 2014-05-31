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


#define VERSION "0.3.0"
#define DRV_NAME "ethpipe"

#define MAX_PKT_LEN	(9014)
#define EP_HDR_LEN	(14)
#define MAX_BUF_LEN	(32)

/* module parameters */
static int debug = 1;

/* pci parameters */
static DEFINE_PCI_DEVICE_TABLE(ep_pci_tbl) = {
	{ 0x3776, 0x8081, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ep_pci_tbl);

/* pci registers */
static unsigned char *mmio0_ptr = 0, *mmio1_ptr = 0;    // mmio pointer
static unsigned long long mmio0_start, mmio0_end, mmio0_flags, mmio0_len;
static unsigned long long mmio1_start, mmio1_end, mmio1_flags, mmio1_len;
unsigned long long *reg_counter;
unsigned long long *reg_lap1, *reg_lap2;

/* workqueue */
static struct workqueue_struct *ep_wq;
struct work_struct work1;

/* procfs */
struct proc_dir_entry *ep_proc_root;    // proc root dir

/* ethpipe */
static int buf_pos = 0;

/* tmp */
unsigned long long counter_data = 9999;
unsigned long long lap1_data = 1111;
unsigned long long lap2_data = 2222;
int counter_tmp, lap1_tmp, lap2_tmp;

/**
 * procfs handle functions
 *
 **/
static ssize_t counter_show( struct file *file, char *buf, size_t count,
				loff_t *ppos )
{
	if (debug)
		pr_info("%s\n", __func__);
	
	sprintf(buf, "%llX\n", *reg_counter);
	return count;
}

static ssize_t lap1_show( struct file *file, char *buf, size_t count,
				loff_t *ppos )
{
	if (debug)
		pr_info("%s\n", __func__);
	
	return sprintf(buf, "%llX\n", *reg_lap1);
}

static ssize_t lap1_store(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if (debug)
		pr_info("%s\n", __func__);
	
	sscanf(buf, "%llX", reg_lap1);
	return count;
}

static ssize_t lap2_show( struct file *file, char *buf, size_t count,
				loff_t *ppos )
{
	if (debug)
		pr_info("%s\n", __func__);
	
	return sprintf(buf, "%llX\n", *reg_lap2);
}

static ssize_t lap2_store(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if (debug)
		pr_info("%s\n", __func__);

	sscanf(buf, "%llX", reg_lap2);
	return count;
}

static const struct file_operations proc_counter_fops = {
	.read = counter_show,
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

/**
 * ep_write
 *
 **/
static ssize_t ep_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned int copy_len = 0;
	static unsigned char pkt[EP_HDR_LEN+MAX_PKT_LEN] = {0};

	if (debug)
		pr_info("%s\n", __func__);

	if (count > (MAX_BUF_LEN - buf_pos)) {
		copy_len = MAX_BUF_LEN - buf_pos;
	} else {
		copy_len = count;
	}

	if ( copy_from_user( pkt+buf_pos, buf, copy_len )) {
		pr_info( KERN_INFO "copy_from_user failed\n" );
		return -EFAULT;
	}

	*ppos += copy_len;
	buf_pos += copy_len;

	if (debug) {
		pr_info( KERN_INFO "buf_pos = %d\n", buf_pos );
	
		pr_info( "DEBUG: pkt = %02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x\n",
			pkt[ 0], pkt[ 1], pkt[ 2], pkt[ 3], pkt[ 4], pkt[ 5], pkt[ 6], pkt[ 7],
			pkt[ 8], pkt[ 9], pkt[10], pkt[11] );
	}

	return count;
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
static int ep_init_one(struct pci_dev *pdev,
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

	/* PCI mmio0 pointers */
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

	/* PCI mmio1 pointers */
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

	/* interrupt handler */
	if (request_irq( pdev->irq, ep_interrupt, IRQF_SHARED, DRV_NAME, pdev)) {
		pr_warn( "cannot request_irq\n" );
	}

	return 0;

err:
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	return -1;
}

/**
 * ep_remove_one
 *
 **/
static void ep_remove_one (struct pci_dev *pdev)
{
	pr_info("%s\n", __func__);

	/* disable interrupts */
	disable_irq(pdev->irq);
	free_irq(pdev->irq, pdev);

	/* detach pci device */
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver ep_pci_driver = {
	.name     = DRV_NAME,
	.id_table = ep_pci_tbl,
	.probe    = ep_init_one,
	.remove   = ep_remove_one,
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
	struct proc_dir_entry *pe_counter, *pe_lap1, *pe_lap2;
	int ret;

	pr_info("%s\n", __func__);

	/* tmp */
	reg_counter = &counter_data;
	reg_lap1 = &lap1_data;
	reg_lap2 = &lap2_data;

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
	pe_counter = proc_create("counter", 0666, ep_proc_root, &proc_counter_fops);
	if (pe_counter == NULL) {
		pr_err("cannot create %s procfs entry\n", "counter");
		ret = -EINVAL;
		goto remove_counter;
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
remove_counter:
	remove_proc_entry("counter", ep_proc_root);
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
		destroy_workqueue(ep_wq);
		ep_wq = NULL;
	}

	/* procfs */
	remove_proc_entry("lap2", ep_proc_root);
	remove_proc_entry("lap1", ep_proc_root);
	remove_proc_entry("counter", ep_proc_root);
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


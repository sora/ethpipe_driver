#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#define DEBUG

#define DRV_NAME	"ethpipe"
#define DRV_VERSION "0.3.0"
#define ETHPIPE_DRIVER_NAME	DRV_NAME " EtherPIPE driver " DRV_VERSION

MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 * ethpipe_open
 *
 **/
static int ethpipe_open(struct inode *inode, struct file *file)
{
	printk("%s\n", __func__);
	return 0;
}

/**
 * ethpipe_write
 *
 **/
static ssize_t ethpipe_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	printk("%s\n", __func__);
	return count;
}

/**
 * ethpipe_release
 *
 **/
static int ethpipe_release(struct inode *inode, struct file *file)
{
	printk("%s\n", __func__);
	return 0;
}

static struct file_operations ethpipe_fops = {
	.owner    = THIS_MODULE,
	.open     = ethpipe_open,
	.write    = ethpipe_write,
	.release  = ethpipe_release,
};

static struct miscdevice ethpipe_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DRV_NAME,
	.fops  = &ethpipe_fops,
};

/**
 * ethpipe_init_one
 *
 **/
static int ethpipe_init_one(void)
{
	static char devname[15];
	static int board_idx = -1;
	int ret;

	++board_idx;
	printk( KERN_INFO "board_idx: %d\n", board_idx );

	/* register ethpipe character device */
	sprintf( devname, "%s/%d", DRV_NAME, board_idx );
	ethpipe_dev.name = devname;
	ret = misc_register(&ethpipe_dev);
	if (ret) {
		printk("Fail to misc_register (MISC_DYNAMIC_MINOR)\n");
		return ret;
	}

	return 0;
}

/**
 * ethpipe_init_module
 *
 **/
static int __init ethpipe_init_module(void)
{
	pr_info(ETHPIPE_DRIVER_NAME "\n");
	return ethpipe_init_one();
}

module_init(ethpipe_init_module);

/**
 * ethpipe_exit_module
 *
 **/
static void __exit ethpipe_exit_module(void)
{
	printk("ethpipe driver unloaded.\n");
	misc_deregister(&ethpipe_dev);
}

module_exit(ethpipe_exit_module);


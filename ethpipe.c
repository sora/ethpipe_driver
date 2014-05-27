#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#define DEBUG

#define DRV_NAME	"ethpipe"
#define DRV_VERSION "0.3.0"
#define ETHPIPE_DRIVER_NAME	DRV_NAME " -- EtherPIPE driver " DRV_VERSION

#define MAX_PKT_LEN	(9014)
#define ETHPIPE_HDR_LEN	(14)

#define MAX_BUF_LEN	(32)

static int buf_pos = 0;


MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 * ethpipe_open
 *
 **/
static int ethpipe_open(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	printk("%s\n", __func__);
#endif

	return 0;
}

/**
 * ethpipe_write
 *
 **/
static ssize_t ethpipe_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	unsigned int copy_len = 0;
	static unsigned char pkt[ETHPIPE_HDR_LEN+MAX_PKT_LEN] = {0};

#ifdef DEBUG
	printk("%s\n", __func__);
#endif

	if (count > (MAX_BUF_LEN - buf_pos)) {
		copy_len = MAX_BUF_LEN - buf_pos;
	} else {
		copy_len = count;
	}

	if ( copy_from_user( pkt+buf_pos, buf, copy_len )) {
		printk( KERN_INFO "copy_from_user failed\n" );
		return -EFAULT;
	}

	*ppos += copy_len;
	buf_pos += copy_len;

#ifdef DEBUG
	printk( KERN_INFO "buf_pos = %d\n", buf_pos );
#endif

#ifdef DEBUG
	printk( "DEBUG: pkt = %02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x\n",
		pkt[ 0], pkt[ 1], pkt[ 2], pkt[ 3], pkt[ 4], pkt[ 5], pkt[ 6], pkt[ 7],
		pkt[ 8], pkt[ 9], pkt[10], pkt[11], pkt[12], pkt[13], pkt[14], pkt[15] );
#endif

	return count;
}

/**
 * ethpipe_release
 *
 **/
static int ethpipe_release(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	printk("%s\n", __func__);
#endif

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
	static char devname[16];
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
	printk("%s\n", __func__);
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
	printk("%s\n", __func__);
	misc_deregister(&ethpipe_dev);
}

module_exit(ethpipe_exit_module);


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/stat.h>

#define DRV_NAME	"ethpipe"
#define VERSION "0.3.0"
#define MAX_PKT_LEN	(9014)
#define ETHPIPE_HDR_LEN	(14)
#define MAX_BUF_LEN	(32)

static int buf_pos = 0;

/* module parameters */
static int debug;

/**
 * ethpipe_open
 *
 **/
static int ethpipe_open(struct inode *inode, struct file *file)
{

  if (debug)
    printk("%s\n", __func__);

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

  if (debug)
    printk("%s\n", __func__);

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

  if (debug) {
    printk( KERN_INFO "buf_pos = %d\n", buf_pos );

    printk( "DEBUG: pkt = %02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x\n",
      pkt[ 0], pkt[ 1], pkt[ 2], pkt[ 3], pkt[ 4], pkt[ 5], pkt[ 6], pkt[ 7],
      pkt[ 8], pkt[ 9], pkt[10], pkt[11] );
  }

	return count;
}

/**
 * ethpipe_release
 *
 **/
static int ethpipe_release(struct inode *inode, struct file *file)
{
  if (debug)
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
static int __init ep_init(void)
{
	pr_info("%s\n", __func__);
	return ethpipe_init_one();
}

/**
 * ethpipe_exit_module
 *
 **/
static void __exit ep_cleanup(void)
{
	printk("%s\n", __func__);
	misc_deregister(&ethpipe_dev);
}

module_init(ep_init);
module_exit(ep_cleanup);

MODULE_AUTHOR("Yohei Kuga <sora@haeena.net>");
MODULE_DESCRIPTION("Ethernet Character device");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debug mode");

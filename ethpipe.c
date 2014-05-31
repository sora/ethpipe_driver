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

#define VERSION "0.3.0"

#define MAX_PKT_LEN	(9014)
#define EP_HDR_LEN	(14)
#define MAX_BUF_LEN	(32)

#define EP_DEV_DIR	"ethpipe"
#define EP_PROC_DIR	"ethpipe"

/* module parameters */
static int debug = 1;

struct proc_dir_entry *ep_proc_root;    /* proc root dir */

static int buf_pos = 0;

unsigned long long *reg_counter;
unsigned long long *reg_lap1, *reg_lap2;

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
		printk("%s\n", __func__);
	
	sprintf(buf, "%llX\n", *reg_counter);
	return count;
}

static ssize_t lap1_show( struct file *file, char *buf, size_t count,
				loff_t *ppos )
{
	if (debug)
		printk("%s\n", __func__);
	
	return sprintf(buf, "%llX\n", *reg_lap1);
}

static ssize_t lap1_store(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if (debug)
		printk("%s\n", __func__);
	
	sscanf(buf, "%llX", reg_lap1);
	return count;
}

static ssize_t lap2_show( struct file *file, char *buf, size_t count,
				loff_t *ppos )
{
	if (debug)
		printk("%s\n", __func__);
	
	return sprintf(buf, "%llX\n", *reg_lap2);
}

static ssize_t lap2_store(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	if (debug)
		printk("%s\n", __func__);

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
		printk("%s\n", __func__);

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
 * ep_release
 *
 **/
static int ep_release(struct inode *inode, struct file *file)
{
	if (debug)
		printk("%s\n", __func__);

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
	.name  = EP_DEV_DIR,
	.fops  = &ep_fops,
};

/**
 * ep_init_one
 *
 **/
static int ep_init_one(void)
{
	static char devname[16];
	static int board_idx = -1;
	struct proc_dir_entry *pe_counter, *pe_lap1, *pe_lap2;
	int ret;

	/* tmp */
	reg_counter = &counter_data;
	reg_lap1 = &lap1_data;
	reg_lap2 = &lap2_data;

	++board_idx;
	printk( KERN_INFO "board_idx: %d\n", board_idx );

	/* register ethpipe character device */
	sprintf( devname, "%s/%d", EP_DEV_DIR, board_idx );
	ep_misc_device.name = devname;
	ret = misc_register(&ep_misc_device);
	if (ret) {
		printk("Fail to misc_register (MISC_DYNAMIC_MINOR)\n");
		return ret;
	}

	/* register ethpipe procfs entries */
	// dir: /proc/ethpipe
	ep_proc_root = proc_mkdir(EP_PROC_DIR, NULL);
	if (!ep_proc_root) {
		pr_warn("cannot create /proc/%s\n", EP_PROC_DIR);
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

	return 0;

remove_lap2:
	remove_proc_entry("lap2", ep_proc_root);
remove_lap1:
	remove_proc_entry("lap1", ep_proc_root);
remove_counter:
	remove_proc_entry("counter", ep_proc_root);
	remove_proc_entry(EP_PROC_DIR, NULL);
	return ret;
}

/**
 * ep_init_module
 *
 **/
static int __init ep_init(void)
{
	pr_info("%s\n", __func__);
	return ep_init_one();
}

/**
 * ep_exit_module
 *
 **/
static void __exit ep_cleanup(void)
{
	printk("%s\n", __func__);
	misc_deregister(&ep_misc_device);

	remove_proc_entry("lap2", ep_proc_root);
	remove_proc_entry("lap1", ep_proc_root);
	remove_proc_entry("counter", ep_proc_root);
	remove_proc_entry(EP_PROC_DIR, NULL);
}

module_init(ep_init);
module_exit(ep_cleanup);

MODULE_AUTHOR("Yohei Kuga <sora@haeena.net>");
MODULE_DESCRIPTION("Ethernet Character device");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION);
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debug mode");


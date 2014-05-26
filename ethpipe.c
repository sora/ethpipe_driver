#include <linux/module.h>

#define DEBUG

#define DRV_NAME	"ethpipe"
#define DRV_VERSION "0.3.0"
#define ETHPIPE_DRIVER_NAME	DRV_NAME " EtherPIPE driver " DRV_VERSION

MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 * ethpipe_init_module
 *
 **/
static int __init ethpipe_init_module(void)
{
  pr_info(ETHPIPE_DRIVER_NAME "\n");

  return 0;
}

module_init(ethpipe_init_module);

/**
 * ethpipe_exit_module
 *
 **/
static void __exit ethpipe_exit_module(void)
{
  printk("ethpipe driver unloaded.\n");
}

module_exit(ethpipe_exit_module);

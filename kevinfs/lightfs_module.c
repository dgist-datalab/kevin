#include <linux/module.h>
#include "lightfs_fs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DGIST");
MODULE_DESCRIPTION("Kevin FS");

static int __init beg_lightfs(void)
{
	return init_lightfs_fs();
}

static void __exit end_lightfs(void)
{
	exit_lightfs_fs();
}


module_init(beg_lightfs);
module_exit(end_lightfs);

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include <asm/unistd.h>
#include <linux/highuid.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <linux/tty.h>
#include <linux/unistd.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/rculist.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/pid_namespace.h>
#include <linux/slab.h>
#include <linux/init_task.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/random.h>

#include "kkit.h"
#include "peekaboo.h"
#include "server.h"
#include "touch.h"
#include "addr_pfn_map.h"

static int __init ovo_init(void) {
    int ret;

    cuteBabyPleaseDontCry();
    ret = 0;

    ret = init_server();
	if(ret) {
		return ret;
	}

	ret = init_input_dev();

	if (!ret) {
		init_addr_pfn_map();
	}

    return ret;
}

static void __exit ovo_exit(void) {
    exit_server();
	exit_input_dev();
	destroy_addr_pfn_map();
    pr_info("[ovo] goodbye!\n");
}

module_init(ovo_init);
module_exit(ovo_exit);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

MODULE_AUTHOR("ovo");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ovo");
MODULE_VERSION("1.0.0");

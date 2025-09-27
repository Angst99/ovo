//
// Created by fuqiuluo on 25-1-22.
//

#ifndef OVO_MEMORY_H
#define OVO_MEMORY_H

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0))
#include <linux/mmap_lock.h>
#define MM_READ_LOCK(mm) mmap_read_lock(mm);
#define MM_READ_UNLOCK(mm) mmap_read_unlock(mm);
#else
#include <linux/rwsem.h>
#define MM_READ_LOCK(mm) down_read(&(mm)->mmap_sem);
#define MM_READ_UNLOCK(mm) up_read(&(mm)->mmap_sem);
#endif

#include "mmuhack.h"
#include "kkit.h"

#ifdef CONFIG_CMA
//#warning CMA is enabled!
#endif

#if !defined(ARCH_HAS_VALID_PHYS_ADDR_RANGE) || defined(MODULE)
static inline int memk_valid_phys_addr_range(phys_addr_t addr, size_t size)
{
	return addr + size <= __pa(high_memory);
}
#define IS_VALID_PHYS_ADDR_RANGE(x,y) memk_valid_phys_addr_range(x,y)
#else
#define IS_VALID_PHYS_ADDR_RANGE(x,y) valid_phys_addr_range(x,y)
#endif

#if !defined(min)
#define min(x, y) ({        \
typeof(x) _min1 = (x);  \
typeof(y) _min2 = (y);  \
(void) (&_min1 == &_min2); /* 类型检查 */ \
_min1 < _min2 ? _min1 : _min2; })
#endif

uintptr_t get_module_base(pid_t pid, char *name, int vm_flag);
uintptr_t get_module_base_bss(pid_t pid, char *name, int vm_flag);

phys_addr_t vaddr_to_phy_addr(struct mm_struct *mm, uintptr_t va);

// 读写进程内存
// 依赖于current，只能在进程上下文中调用
// 使用ioremap_cache去映射物理地址，然后copy_to_user
int read_process_memory_ioremap(pid_t pid, void __user* addr, void __user* dest, size_t size);
int write_process_memory_ioremap(pid_t pid, void __user* addr, void __user* src, size_t size);

// 读写进程内存
//修改页表属性，无缓存读取
int read_process_memory_pt_read(pid_t pid, void __user* addr, void __user* dest, size_t size);
int write_process_memory_pt_read(pid_t pid, void __user* addr, void __user* src, size_t size);

// 读取进程内存（一定不能是设备内存）
// 通过直接映射区映射到内核虚拟地址空间
int read_process_memory(pid_t pid, void __user* addr, void __user* dest, size_t size);
int write_process_memory(pid_t pid, void __user* addr, void __user* src, size_t size);

// 读写进程内存
// 不依赖于current，可以在任何上下文中调用
// 使用access_process_vm去读写进程内存
int access_process_vm_by_pid(pid_t from, void __user* from_addr, pid_t to, void __user* to_addr, size_t size);

#endif //OVO_MEMORY_H

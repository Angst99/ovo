//
// Created by fuqiuluo on 25-2-3.
//
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

#include "server.h"
#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/vmalloc.h>
#include <net/busy_poll.h>
#include "kkit.h"
#include "memory.h"
#include "touch.h"
#include "vma.h"
#include "addr_pfn_map.h"

static int ovo_release(struct socket *sock) {
	struct sock *sk = sock->sk;

	if (!sk) {
		return 0;
	}

	struct ovo_sock *os = (struct ovo_sock *) ((char *) sock->sk + sizeof(struct sock));

	for (int i = 0; i < os->cached_count; i++) {
		if (os->cached_kernel_pages[i]) {
			free_page(os->cached_kernel_pages[i]);
		}
	}

	sock_orphan(sk);
	sock_put(sk);

	//pr_info("[ovo] OVO socket released!\n");
	return 0;
}

static __poll_t ovo_poll(struct file *file, struct socket *sock,
						 struct poll_table_struct *wait) {
	return 0;
}

static int ovo_setsockopt(struct socket *sock, int level, int optname,
						  sockptr_t optval, unsigned int optlen)
{
	switch (optname) {
		default:
			break;
	}

	return -ENOPROTOOPT;
}

__always_inline int ovo_get_process_pid(int len, char __user *process_name_user) {
	int err;
	pid_t pid;
	char* process_name;

	process_name = kmalloc(len, GFP_KERNEL);
	if (!process_name) {
		return -ENOMEM;
	}

	if (copy_from_user(process_name, process_name_user, len)) {
		err = -EFAULT;
		goto out_proc_name;
	}

	pid = find_process_by_name(process_name);
	if (pid < 0) {
		err = -ESRCH;
		goto out_proc_name;
	}

	err = put_user((int) pid, (pid_t*) process_name_user);
	if (err)
		goto out_proc_name;

	out_proc_name:
	kfree(process_name);
	return err;
}

__always_inline int ovo_get_process_module_base(int len, pid_t pid, char __user *module_name_user, int flag) {
	int err;
	char* module_name;

	module_name = kmalloc(len, GFP_KERNEL);
	if (!module_name) {
		return -ENOMEM;
	}

	if (copy_from_user(module_name, module_name_user, len)) {
		err = -EFAULT;
		goto out_module_name;
	}

	uintptr_t base = get_module_base(pid, module_name, flag);
	if (base == 0) {
		err = -ENAVAIL;
		goto out_module_name;
	}

	err = put_user((uintptr_t) base, (uintptr_t*) module_name_user);
	if (err)
		goto out_module_name;

	out_module_name:
	kfree(module_name);
	return err;
}

/*
 * Don't worry about why the varname here is wrong,
 * in fact, this operation is similar to using ContentProvider to interact with Xposed module in Android,
 * and that thing is also wrong!
 */
static int ovo_getsockopt(struct socket *sock, int level, int optname,
						  char __user *optval, int __user *optlen)
{
	struct sock* sk;
	struct ovo_sock* os;
	int len, alive, ret;
	unsigned long pfn;

	sk = sock->sk;
	if (!sk)
		return -EINVAL;
	os = ((struct ovo_sock*)((char *) sock->sk + sizeof(struct sock)));

	pr_debug("[ovo] getsockopt: %d\n", optname);
	switch (optname) {
		case REQ_GET_PROCESS_PID: {
			ret = ovo_get_process_pid(level, optval);
			if (ret) {
				pr_err("[ovo] ovo_get_process_pid failed: %d\n", ret);
			}
			break;
		}
		case REQ_IS_PROCESS_PID_ALIVE: {
			alive = is_pid_alive(level);
			if (put_user(alive, optlen)) {
				return -EAGAIN;
			}
			ret = 0;
			break;
		}
		case REQ_ATTACH_PROCESS: {
			if(is_pid_alive(level) == 0) {
				return -ESRCH;
			}
			os->pid = level;
			pr_info("[ovo] attached process: %d\n", level);
			ret = 0;
			break;
		}
		case REQ_ACCESS_PROCESS_VM: {
			if (get_user(len, optlen))
				return -EFAULT;

			if (len < sizeof(struct req_access_process_vm))
				return -EINVAL;

			struct req_access_process_vm req;
			if (copy_from_user(&req, optval, sizeof(struct req_access_process_vm)))
				return -EFAULT;

			ret = access_process_vm_by_pid(req.from, req.from_addr, req.to, req.to_addr, req.size);
			break;
		}
		default:
			ret = 114514;
			break;
	}

	if (ret <= 0) {
		// If negative values are not returned,
		// some checks will be triggered? but why?
		// It will change the return value of the function! I return 0, but it will return -1!?
		if(ret == 0) {
			return -2033;
		} else {
			return ret;
		}
	}

	// The following need to attach to a process!
	// u should check whether the attached process is legitimate
	if (os->pid <= 0 || is_pid_alive(os->pid) == 0) {
		return -ESRCH;
	}

	switch (optname) {
		case REQ_GET_PROCESS_MODULE_BASE: {
			if (get_user(len, optlen))
				return -EFAULT;

			if (len < 0)
				return -EINVAL;

			ret = ovo_get_process_module_base(len, os->pid, optval, level);
			break;
		}
		case REQ_READ_PROCESS_MEMORY_IOREMAP: {
			if((ret = read_process_memory_ioremap(os->pid, (void *) optval, (void *) optlen, level))) {
				pr_debug("[ovo] read_process_memory_ioremap failed: %d\n", ret);
			}
			break;
		}
		case REQ_WRITE_PROCESS_MEMORY_IOREMAP: {
			ret = write_process_memory_ioremap(os->pid, (void *) optval, (void *) optlen, level);
			break;
		}
		case REQ_READ_PROCESS_MEMORY_PT_READ: {
			if((ret = read_process_memory_pt_read(os->pid, (void *) optval, (void *) optlen, level))) {
				pr_debug("[ovo] read_process_memory_pt_read failed: %d\n", ret);
			}
			break;
		}
		case REQ_WRITE_PROCESS_MEMORY_PT_READ: {
			ret = write_process_memory_pt_read(os->pid, (void *) optval, (void *) optlen, level);
			break;
		}
		case REQ_READ_PROCESS_MEMORY: {
			ret = read_process_memory(os->pid, (void *) optval, (void *) optlen, level);
			break;
		}
		case REQ_WRITE_PROCESS_MEMORY: {
			ret = write_process_memory(os->pid, (void *) optval, (void *) optlen, level);
			break;
		}
		case REMAP_MEMORY: {
			if (atomic_cmpxchg(&os->remap_in_progress, 0, 1) != 0)
				return -EBUSY;

			ret = process_vaddr_to_pfn(os->pid, optval, &pfn, level);
			if (!ret) {
				os->pfn = pfn;
			} else {
				atomic_set(&os->remap_in_progress, 0);
				os->pfn = 0;
			}

			break;
		}
		default:
			ret = 114514;
			break;
	}

	if (ret <= 0) {
		if(ret == 0) {
			return -2033;
		} else {
			return ret;
		}
	}

	return -EOPNOTSUPP;
}

int ovo_mmap(struct file *file, struct socket *sock,
				 struct vm_area_struct *vma) {
	int ret;
	struct ovo_sock *os;

	if (!sock->sk) {
		return -EINVAL;
	}
	os = (struct ovo_sock*)((char *) sock->sk + sizeof(struct sock));

	atomic_set(&os->remap_in_progress, 0);

	if (os->pid <= 0 || is_pid_alive(os->pid) == 0) {
		return -ESRCH;
	}

	if (!os->pfn) {
		return -EFAULT;
	}

	if (system_supports_mte()) {
		vm_flags_set(vma, VM_MTE);
	}
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	//vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	ret = remap_process_memory(vma, os->pfn, vma->vm_end - vma->vm_start);
	if (!ret) {
		pr_err("[ovo] remap_process_memory failed: %d\n", ret);
	}
	return ret;
}

int ovo_ioctl(struct socket * sock, unsigned int cmd, unsigned long arg) {
	struct event_pool* pool;
	unsigned long flags;

	pool = get_event_pool();
	if (pool == NULL) {
		return -ECOMM;
	}

	struct touch_event_base __user* event_user = (struct touch_event_base __user*) arg;
	struct touch_event_base event;

	if(!event_user) {
		return -EBADR;
	}

	if (copy_from_user(&event, event_user, sizeof(struct touch_event_base))) {
		return -EACCES;
	}

	if (cmd == CMD_TOUCH_CLICK_DOWN) {
		spin_lock_irqsave(&pool->event_lock, flags);

		if (pool->size >= MAX_EVENTS) {
			pr_warn("[ovo] event pool is full!\n");
			pool->size = 0;
		}

		input_event_cache(EV_ABS, ABS_MT_SLOT, event.slot, 0);
		int id = input_mt_report_slot_state_with_id_cache(MT_TOOL_FINGER, 1, event.slot, 0);
		input_event_cache(EV_ABS, ABS_MT_POSITION_X, event.x, 0);
		input_event_cache(EV_ABS, ABS_MT_POSITION_Y, event.y, 0);
		input_event_cache(EV_ABS, ABS_MT_PRESSURE, event.pressure, 0);
		input_event_cache(EV_ABS, ABS_MT_TOUCH_MAJOR, event.pressure, 0);
		input_event_cache(EV_ABS, ABS_MT_TOUCH_MINOR, event.pressure, 0);

		event.pressure = id;
		if (copy_to_user(event_user, &event, sizeof(struct touch_event_base))) {
			pr_err("[ovo] copy_to_user failed: %s\n", __func__);
			return -EACCES;
		}

		spin_unlock_irqrestore(&pool->event_lock, flags);
		return -2033;
	}
	if (cmd == CMD_TOUCH_CLICK_UP) {
		spin_lock_irqsave(&pool->event_lock, flags);

		if (pool->size >= MAX_EVENTS) {
			pr_warn("[ovo] event pool is full!\n");
			pool->size = 0;
		}

		input_event_cache(EV_ABS, ABS_MT_SLOT, event.slot, 0);
		input_mt_report_slot_state_cache(MT_TOOL_FINGER, 0, 0);

		spin_unlock_irqrestore(&pool->event_lock, flags);
		return -2033;
	}
	if (cmd == CMD_TOUCH_MOVE) {
		spin_lock_irqsave(&pool->event_lock, flags);

		if (pool->size >= MAX_EVENTS) {
			pr_warn("[ovo] event pool is full!\n");
			pool->size = 0;
		}

		input_event_cache(EV_ABS, ABS_MT_SLOT, event.slot, 0);
		input_event_cache(EV_ABS, ABS_MT_POSITION_X, event.x, 0);
		input_event_cache(EV_ABS, ABS_MT_POSITION_Y, event.y, 0);
		input_event_cache(EV_SYN, SYN_MT_REPORT, 0, 0);

		spin_unlock_irqrestore(&pool->event_lock, flags);
		return -2033;
	}

	if (cmd == CMD_COPY_PROCESS) {
		if (!sock->sk) {
			return -EINVAL;
		}
		const struct ovo_sock *os = (struct ovo_sock *) ((char *) sock->sk + sizeof(struct sock));
		if (os->pid == 0) {
			return -ESRCH;
		}

		struct copy_process_args args;
		if (copy_from_user(&args, (struct copy_process_args __user*) arg, sizeof(struct copy_process_args))) {
			return -EACCES;
		}

		struct kernel_clone_args clone_args = {0};
		clone_args.flags = CLONE_VM | CLONE_THREAD | CLONE_SIGHAND | CLONE_FILES;  // 共享地址空间等资源
		clone_args.stack = 0;
		clone_args.stack_size = 0;
		clone_args.fn = args.fn;
		clone_args.fn_arg = args.arg;
		clone_args.tls = 0;
		clone_args.exit_signal = 0;

		struct pid *pid_struct = find_get_pid(os->pid);
		int node = numa_node_id();

		static struct task_struct *(*my_copy_process)(struct pid *pid, int trace, int node,
				 struct kernel_clone_args *args) = NULL;
		if (my_copy_process == NULL) {
			my_copy_process = (void*) ovo_kallsyms_lookup_name("copy_process");
		}

		if (my_copy_process == NULL) {
			pr_err("[ovo] copy_process not found!\n");
			return -EFAULT;
		}

		struct task_struct *new_task = my_copy_process(pid_struct, 0, node, &clone_args);
		put_pid(pid_struct);

		if (!new_task) {
			pr_err("[ovo] copy_process failed!\n");
			return -EFAULT;
		}

		return -2033;
	}

	if (cmd == CMD_PROCESS_MALLOC) {
		if (!sock->sk) {
			return -EINVAL;
		}

		struct ovo_sock *os = (struct ovo_sock *) ((char *) sock->sk + sizeof(struct sock));
		if (os->pid == 0) {
			return -ESRCH;
		}

		int writable = 0;
		if (get_user(writable, (int*) arg)) {
			return -EACCES;
		}

		if (os->cached_count >= MAX_CACHE_KERNEL_ADDRESS_COUNT) {
			pr_err("[ovo] cached_addr_array is full!\n");
			return -ENOMEM;
		}

		if (atomic_cmpxchg(&os->remap_in_progress, 0, 1) != 0)
			return -EBUSY;

		struct pid *pid_struct = find_get_pid(os->pid);
		if (!pid_struct) {
			pr_err("[ovo] failed to find pid_struct: %s\n", __func__);
			return -ESRCH;
		}

		struct task_struct *task = get_pid_task(pid_struct, PIDTYPE_PID);
		put_pid(pid_struct);
		if(!task) {
			pr_err("[ovo] failed to get task from pid_struct: %s\n", __func__);
			return -ESRCH;
		}

		struct mm_struct *mm = get_task_mm(task);
		put_task_struct(task);
		if (!mm) {
			pr_err("[ovo] failed to get mm from task: %s\n", __func__);
			return -ESRCH;
		}

		MM_READ_LOCK(mm)
		unsigned long addr = 0;
		get_unmapped_area_mm(mm, &addr, PAGE_SIZE);

		if (addr == 0) {
			MM_READ_UNLOCK(mm)
			mmput(mm);
			atomic_set(&os->remap_in_progress, 0);
			pr_err("[ovo] get_unmapped_area_mm failed: %s\n", __func__);
			return -ENOMEM;
		}

		if (alloc_process_special_memory_mm(mm, addr, PAGE_SIZE, writable)) {
			MM_READ_UNLOCK(mm)
			mmput(mm);
			atomic_set(&os->remap_in_progress, 0);
			pr_err("[ovo] alloc_process_special_memory_mm failed: %s\n", __func__);
			return -ENOMEM;
		}

		MM_READ_UNLOCK(mm)
		mmput(mm);

		unsigned long kaddr = get_zeroed_page(GFP_KERNEL);
		if (!kaddr) {
			pr_err("[ovo] kmalloc failed!: %s\n", __func__);
			atomic_set(&os->remap_in_progress, 0);
			return -ENOMEM;
		}

		if (put_user(addr, (unsigned long __user*) arg)
			|| put_user((unsigned long) PAGE_SIZE, (unsigned long __user*) (arg + sizeof(unsigned long)))) {
			free_page(kaddr);
			atomic_set(&os->remap_in_progress, 0);
			return -EACCES;
		}

		unsigned long pfn = __phys_to_pfn(__virt_to_phys(kaddr));
		if (insert_addr_pfn(addr, pfn) < 0) {
			free_page(kaddr);
			atomic_set(&os->remap_in_progress, 0);
			return -EEXIST;
		}

		os->cached_kernel_pages[os->cached_count++] = kaddr;
		os->pfn = pfn;

		pr_info("[ovo] malloced kernel address: 0x%lx, pfn: 0x%lx, magic: 0x%lx\n", kaddr, pfn, *(unsigned long*) kaddr);
		return -2033;
	}

	if (cmd == CMD_HIDE_VMA) {
		if (!sock->sk) {
			return -EINVAL;
		}

		struct ovo_sock *os = (struct ovo_sock *) ((char *) sock->sk + sizeof(struct sock));
		if (os->pid == 0) {
			return -ESRCH;
		}

		struct hide_vma_args args;
		if (copy_from_user(&args, (struct hide_vma_args __user*) arg, sizeof(struct hide_vma_args))) {
			pr_err("[ovo] copy_from_user failed: %s\n", __func__);
			return -EACCES;
		}

		struct vm_area_struct *vma = find_vma_pid(os->pid, args.ptr);
		if (!vma) {
			return -ESRCH;
		}

		if (args.mode == HIDE_X) {
			vm_flags_clear(vma, VM_EXEC);
		} else {
			pr_warn("[ovo] hide mode not supported!\n");
			return -ENOSYS;
		}

		return -2033;
	}

	return -ENOTTY;
}

int ovo_sendmsg(struct socket *sock, struct msghdr *m,
                size_t total_len
) {
	return 0;
}

static struct proto ovo_proto = {
	.name = "NFC_LLCP",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct sock) + sizeof(struct ovo_sock),
};

static struct proto_ops ovo_proto_ops = {
	.family = PF_DECnet,
	.owner = THIS_MODULE,
	.release = ovo_release,
	.bind = sock_no_bind,
	.connect = sock_no_connect,
	.socketpair = sock_no_socketpair,
	.accept = sock_no_accept,
	.getname = sock_no_getname,
	.poll		= ovo_poll,
	.ioctl		= ovo_ioctl,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= ovo_setsockopt,
	.getsockopt	= ovo_getsockopt,
	.sendmsg	= ovo_sendmsg,
	.recvmsg	= sock_no_recvmsg,
	.mmap		= ovo_mmap
};

static int free_family = AF_DECnet;

static int ovo_create(struct net *net, struct socket *sock, int protocol,
					  int kern)
{
	uid_t caller_uid;
	struct sock *sk;
	struct ovo_sock *os;

	caller_uid = *((uid_t*) &current_cred()->uid);
	if (caller_uid != 0) {
		pr_warn("[ovo] Only root can create OVO socket!\n");
		return -EAFNOSUPPORT;
	}

	if (sock->type != SOCK_RAW) {
		//pr_warn("[ovo] a OVO socker must be SOCK_RAW!\n");
		return -ENOKEY;
	}

	sock->state = SS_UNCONNECTED;

	sk = sk_alloc(net, PF_INET, GFP_KERNEL, &ovo_proto, kern);
	if (!sk) {
		pr_warn("[ovo] sk_alloc failed!\n");
		return -ENOBUFS;
	}

	os = (struct ovo_sock*)((char *) sk + sizeof(struct sock));

	ovo_proto_ops.family = free_family;
	sock->ops = &ovo_proto_ops;
	sock_init_data(sock, sk);

	// Initialize the ovo_sock
	os->pid = 0;
	os->pfn = 0;
	atomic_set(&os->remap_in_progress, 0);
	os->cached_count = 0;

	return 0;
}

static struct net_proto_family ovo_family_ops = {
	.family = PF_DECnet,
	.create = ovo_create,
	.owner	= THIS_MODULE,
};

static int register_free_family(void) {
	int family;
	int err;
	for(family = free_family; family < NPROTO; family++) {
		ovo_family_ops.family = family;
		err = sock_register(&ovo_family_ops);
		if (err)
			continue;
		else {
			free_family = family;
			pr_info("[ovo] Find free proto_family: %d\n", free_family);
			return 0;
		}
	}

	pr_err("[ovo] Can't find any free proto_family!\n");
	return err;
}

int init_server(void) {
	int err;

	err = proto_register(&ovo_proto, 1);
	if (err)
		goto out;

	err = register_free_family();
	if (err)
		goto out_proto;

	return 0;

	sock_unregister(free_family);
	out_proto:
	proto_unregister(&ovo_proto);
	out:
	return err;
}

void exit_server(void) {
	sock_unregister(free_family);
	proto_unregister(&ovo_proto);
}

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/types.h>
#include <linux/tcp.h>
#include <net/sock.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/completion.h>
#include <asm/atomic.h>

#include "myheader.h"

#define ERROR_PRINT(func) \
	do { \
		printk(KERN_ALERT MODULE_NAME ": " #func " failed: %s:%d\n", \
				__func__, __LINE__); \
    } while (0)


MODULE_DESCRIPTION("TCP Echo server in Linux kernel");
MODULE_AUTHOR("Hiroki Watanabe");
MODULE_LICENSE("Dual BSD/GPL");

static struct socket *sock;

static struct task_struct *accept_kth;
//DECLARE_COMPLETION(accept_cpl);
static struct completion accept_cpl;

struct client {
	struct work_struct work;
	struct task_struct *rw_kth;
	struct socket *rw_sock;
	struct list_head cl_list;
	struct completion cl_cpl;
	char *buf;
};

static struct workqueue_struct *wq;
static struct list_head accept_list;



static void rw_func(struct work_struct *work)
{
	struct msghdr msg;
	struct client *cl = container_of(work, struct client, work);
	struct socket *sock_rw  = cl->rw_sock;
	struct kvec vec;
	int len;
	printk(KERN_INFO MODULE_NAME ": rw_func: kthread=%p\n", current);
	
	cl->rw_kth = current;
	allow_signal(SIGTERM);
	allow_signal(SIGKILL);
	/* recv and send */
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = (MSG_NOSIGNAL);
	
	vec.iov_len = MSG_SIZE;
	vec.iov_base = kmalloc(MSG_SIZE, GFP_KERNEL);

	do  {
		memset(vec.iov_base, 0, MSG_SIZE);
		len = kernel_recvmsg(sock_rw, &msg, &vec, 1, MSG_SIZE, msg.msg_flags);
		printk(KERN_ALERT MODULE_NAME ": sock->state:%d\n", sock_rw->state);
		if (len < 0) {
			printk(KERN_ALERT MODULE_NAME ": rw_sokc:%p, err:%d\n", sock_rw, len);
			ERROR_PRINT(sock_recvmsg);
			if (signal_pending(current)) {
				printk(KERN_ALERT MODULE_NAME ": signal_pending(current) == true\n");
				break;
			}
		}
		printk(KERN_INFO MODULE_NAME ": recv: %s (%d)\n", (char *)vec.iov_base, len);
	} while (len > 0);

	kfree(vec.iov_base);
	kernel_sock_shutdown(sock_rw, SHUT_RDWR);
	sock_release(sock_rw);
	//complete(&cl->cl_cpl);
	disallow_signal(SIGTERM);
	disallow_signal(SIGKILL);
	printk(KERN_INFO MODULE_NAME ": stop rw_kth\n");
}

static int accept_func(void *arg)
{
	int ret;
	struct client *cl_p;
	struct client *tmp;
	allow_signal(SIGTERM);
	allow_signal(SIGKILL);

	printk(KERN_INFO MODULE_NAME ": accept_func: kthread=%p\n", current);

	wq = alloc_workqueue("rw_queue", WQ_UNBOUND, MAX_CLIENTS);
	if (!wq) {
		printk(KERN_ALERT MODULE_NAME ": cannot start read/write workqueue\n");
		return -EFAULT;
	}
	INIT_LIST_HEAD(&accept_list);

	while (1) {
		struct client *cl;

		cl = kmalloc(sizeof(struct client), GFP_KERNEL);
		if (!cl) {
			ERROR_PRINT(kmalloc:cl);
			return -EFAULT;
		}

		printk(KERN_INFO MODULE_NAME ": wait for a new client arrival...\n");
		ret = kernel_accept(sock, &cl->rw_sock, 0);
		printk(KERN_INFO MODULE_NAME ": ret(kernel_accept) = %d, rw_sock:%p\n", ret, cl->rw_sock);

		if (kthread_should_stop()) {
			printk(KERN_ALERT MODULE_NAME ": GET INTERRUPTION: BRINGING DOWN...\n");
			if (signal_pending(current)) {
				printk(KERN_ALERT MODULE_NAME ": signal_pending(current) == true\n");
				break;
			}
		}

		if (ret == -EINVAL) {
			printk(KERN_ALERT "err:%d\n", ret);
			continue;
		}

		if (ret < 0) {
			printk(KERN_ALERT "err:%d\n", ret);
			ERROR_PRINT(kernel_accept);
			continue;
		}
		
		list_add(&cl->cl_list, &accept_list);
		//cl->rw_sock = rw_sock;
		init_completion(&cl->cl_cpl);
		INIT_WORK(&cl->work, rw_func);
		queue_work(wq, &cl->work);
	}

	list_for_each_entry_safe(cl_p, tmp, &accept_list, cl_list) {
		list_del(&cl_p->cl_list);
		send_sig(SIGTERM, cl_p->rw_kth, 0);
		//wait_for_completion_interruptible(&cl_p->cl_cpl);
		kfree(cl_p);
		printk(KERN_INFO MODULE_NAME ": delete client\n");
	}
	printk(KERN_INFO MODULE_NAME ": destroying workqueue...\n");
	destroy_workqueue(wq);
	printk(KERN_INFO MODULE_NAME ": workqueue destroyed...\n");
	printk(KERN_INFO MODULE_NAME ": stop accept_kth\n");

	disallow_signal(SIGTERM);
	disallow_signal(SIGKILL);

	complete_and_exit(&accept_cpl, 0);
}

static int kecho_init(void)
{
	int ret = 0;
	struct sockaddr_in addr;
	int opt = 1;

	init_completion(&accept_cpl);

	printk(KERN_INFO MODULE_NAME ": start loading...\n");

	/* socket open */
	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0) {
		ERROR_PRINT(sock_create);
		goto out;
	}

	/* bind */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(PORT);
	ret = kernel_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

	ret = kernel_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		printk(KERN_ALERT MODULE_NAME ": err: %d\n", ret);
		ERROR_PRINT(kernel_bind);
		goto release_out;
	}
	
	/* listen */
	ret = kernel_listen(sock, 5);
	if (ret < 0) {
		ERROR_PRINT(kernel_listen);
		goto release_out;
	}

	/* accept (in another thread) */
	accept_kth = kthread_run(accept_func, NULL, "accept_kth");
	if (IS_ERR(accept_kth)) {
		ERROR_PRINT(kthread_run);
		goto shutdown_out;
	}
	printk(KERN_INFO MODULE_NAME ": running accept kthread...\n");


	printk(KERN_INFO MODULE_NAME ": successfully loaded.\n");

	return ret;


shutdown_out:
	kernel_sock_shutdown(sock, SHUT_RDWR);

release_out:
	sock_release(sock);

out:
	ret = -EFAULT;
	return ret;
}

static void kecho_exit(void)
{
	int err;


	printk(KERN_INFO MODULE_NAME ": start unloading...\n");
	/* wait for stopping othre threads */
	err = wake_up_process(accept_kth);
	printk(KERN_INFO MODULE_NAME ": wake_up_process:ret %d\n", err);
	send_sig(SIGTERM, accept_kth, 0);
	kthread_stop(accept_kth);

	/* close listen socket */
	printk(KERN_INFO MODULE_NAME ": shutdown listen sock\n");
	err = kernel_sock_shutdown(sock, SHUT_RDWR);
	if (err < 0) { 
		ERROR_PRINT(kernel_sock_shutdown); 
	} 
	printk(KERN_INFO MODULE_NAME ": sock_release\n");
	sock_release(sock);

	wait_for_completion_interruptible(&accept_cpl);
	printk(KERN_INFO MODULE_NAME ": wait_for_completion:accept_kth done!\n");
	printk(KERN_INFO MODULE_NAME ": successfully unloaded\n");
}


module_init(kecho_init);
module_exit(kecho_exit);

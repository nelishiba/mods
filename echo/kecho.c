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
MODULE_LICENSE("Dual GPL/BSD");

static struct socket *sock;

static struct task_struct *accept_kth;
//DECLARE_COMPLETION(accept_cpl);
static struct completion accept_cpl;

struct client {
	struct task_struct *rw_kth;
	struct socket *rw_sock;
	char *buf;
};



static struct client clients[MAX_CLIENTS];

static atomic_t nr_clients;

static int rw_func(void *arg)
{
	struct msghdr msg;
	int ret = 0;
	struct socket *sock_rw = ((struct client *)arg)->rw_sock;
	struct kvec vec;
	int len;

	/* recv and send */
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	
	vec.iov_len = MSG_SIZE;
	vec.iov_base = kmalloc(MSG_SIZE, GFP_KERNEL);

	do  {
		len = kernel_recvmsg(sock_rw, &msg, &vec, MSG_SIZE, MSG_SIZE, msg.msg_flags);
		printk(KERN_ALERT MODULE_NAME ": sock->state:%d\n", sock_rw->state);
		if (sock_rw->state == SS_DISCONNECTING) {
			
		}
		if (len < 0) {
			printk(KERN_ALERT "err:%d\n", len);
			ERROR_PRINT(kernel_recvmsg);
			break;
		}
		printk(KERN_INFO MODULE_NAME "recv: %s (%d)\n", (char *)vec.iov_base, len);
	} while (len > 0);


	kfree(vec.iov_base);
	kernel_sock_shutdown(sock_rw, SHUT_RDWR);
	sock_release(sock_rw);
	
	printk(KERN_INFO MODULE_NAME ": stop rw_kth\n");
	atomic_dec(&nr_clients);

	return ret;
}

static int accept_func(void *arg)
{
	int ret;
	int cnt;
	int i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		clients[i].rw_kth = kthread_create(rw_func, &clients[i], "rw_kth:%d", i);
	}

	while (1) {
		cnt = atomic_read(&nr_clients);
		printk(KERN_INFO MODULE_NAME ": wait for a new client arrival...\n");
		ret = kernel_accept(sock, &clients[cnt].rw_sock, 0);

		if (!kthread_should_stop()) {
			printk(KERN_ALERT MODULE_NAME ": GET INTERRUPTION: BRINGING DOWN...\n");
			break;
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
		clients[cnt].rw_kth = kthread_run(rw_func, &clients[cnt], "rw_kth:%d", cnt);
		if (IS_ERR(clients[cnt].rw_kth)) {
			ERROR_PRINT(kthread_run);
			continue;
		}
		atomic_inc(&nr_clients);

		/* check whether accepting a new client or not */
		while (atomic_read(&nr_clients) == MAX_CLIENTS || !kthread_should_stop()) {
			schedule();
		}
	}

	/* wait for all rw_kth stopping themselves */
	while (atomic_read(&nr_clients) != 0) {
		schedule();
	}
	
	printk(KERN_INFO MODULE_NAME ": stop accept_kth\n");

	complete_and_exit(&accept_cpl, 0);
}

static int kecho_init(void)
{
	int ret = 0;
	struct sockaddr_in addr;

	atomic_set(&nr_clients, 0);
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

	ret = kernel_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
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
	kthread_stop(accept_kth);
	err = wake_up_process(accept_kth);
	printk(KERN_INFO MODULE_NAME ": wake_up_process:ret %d\n", err);

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

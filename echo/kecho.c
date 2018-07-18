#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/types.h>
#include <linux/tcp.h>
#include <net/sock.h>
#include <linux/kthread.h>
#include <linux/kernel.h>

#define MODULE_NAME "Kernel-ECHO"
#define PORT 8880
#define MSG_SIZE 20

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
static struct task_struct *rw_kth;

static int rw_func(void *arg)
{
	struct msghdr msg;
	int ret = 0;
	struct socket *sock_rw = arg;
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

	len = kernel_recvmsg(sock_rw, &msg, &vec, MSG_SIZE, MSG_SIZE, msg.msg_flags);
	if (len < 0) {
		ERROR_PRINT(kernel_recvmsg);
	}


	return ret;
}

static int accept_func(void *arg)
{
	int ret;
	int count = 0;
	struct socket *sock_rw;
	allow_signal(SIGTERM);

	while (!kthread_should_stop()) {
		ret = kernel_accept(sock, &sock_rw, 0);
		if (ret < 0) {
			ERROR_PRINT(kernel_accept);
			continue;
		}
		rw_kth = kthread_run(rw_func, &sock_rw, "rw_kth:%d", count++);
		if (IS_ERR(rw_kth)) {
			ERROR_PRINT(kthread_run);
			continue;
		}
	}
	printk(KERN_INFO MODULE_NAME "stop accept_kth\n");

	return 0;	
}

static int kecho_init(void)
{
	int ret = 0;
	struct sockaddr_in addr;

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
	send_sig(SIGTERM, accept_kth, 0);

	/* stop othre threads */
	kthread_stop(accept_kth);
	//kthread_stop(rw_kth);

	/* close listen socket */
	err = kernel_sock_shutdown(sock, SHUT_RDWR);
	if (err < 0) { 
		ERROR_PRINT(kernel_sock_shutdown); 
	} 
	sock_release(sock);

	/* close read/write socket */

	printk(KERN_INFO MODULE_NAME ": successfully unloaded\n");
}


module_init(kecho_init);
module_exit(kecho_exit);

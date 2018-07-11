#include <linux/init.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/types.h>
#include <linux/tcp.h>
#include <net/sock.h>

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
static struct socket *sock_rw;

static int kecho_init(void)
{
	int ret = 0;
	struct sockaddr_in addr;
	struct kvec vec;
	struct msghdr msg;
	int len;

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
	/* accept */
	ret = kernel_accept(sock, &sock_rw, 0);
	if (ret < 0) {
		ERROR_PRINT(kernel_accept);
		goto shutdown_out;
	}
	
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
		goto shutdown_rw_out;
	}

	printk(KERN_INFO MODULE_NAME ": successfully loaded.\n");

	return ret;

shutdown_rw_out:
	kernel_sock_shutdown(sock_rw, SHUT_RDWR);
	sock_release(sock_rw);

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

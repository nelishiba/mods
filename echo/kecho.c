#include <linux/init.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/types.h>
#include <linux/tcp.h>
#include <net/sock.h>

#define MODULE_NAME "Kernel-ECHO"

#define ERROR_PRINT(func) \
	do { \
		printk(KERN_ALERT MODULE_NAME ": " #func " failed: %s:%d\n", \
				__func__, __LINE__); \
    } while (0)

MODULE_DESCRIPTION("TCP Echo server in Linux kernel");
MODULE_AUTHOR("Hiroki Watanabe");
MODULE_LICENSE("Dual GPL/BSD");

static struct msghdr *buf;
static struct socket *sock;

static int kecho_init(void)
{
	printk(KERN_ALERT MODULE_NAME ": start loading...\n");
	int ret = 0;
	struct socket *sock;
	struct sockaddr_in addr;

	/* socket open */
	ret = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0) {
		ERROR_PRINT(sock_create);
		ret = -EFAULT;
		goto out;
	}
	/* bind */
	ret = kernel_bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		ERROR_PRINT(kernel_bind);
		ret = -EFAULT;
		goto out;
	}
	
	/* listen */
	ret = kernel_listen(sock, 5);
	if (ret < 0) {
		ERROR_PRINT(kernel_listen);
		ret = -EFAULT;
		goto out;
	}
	/* accept */

	
	/* recv and send */

	printk(KERN_ALERT MODULE_NAME ": successfully loaded.\n");

out:
	return ret;
}

static void kecho_exit(void)
{
	/* close listen socket */
	kernel_sock_shutdown(sock, SHUT_RDWR);
	sock_release(sock);

	/* close read/write socket */
	return;
}


module_init(kecho_init);
module_exit(kecho_exit);

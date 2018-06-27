#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>

MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_NAME "devone"

static int devone_devs = 1; /* device count */
static unsigned int devone_major = 0;
module_param(devone_major, uint, 0);
static struct cdev devone_cdev;


struct devone_data {
	unsigned char val;
	rwlock_t lock;
};

static int devone_open(struct inode *inode, struct file *file)
{
	printk("%s: major %d, minor %d (pid %d) \n", __func__,
			imajor(inode),
			iminor(inode),
			current->pid
		  );

	inode->i_private = inode;
	file->private_data = file;

	printk("  i_private=%p private_data=%p\n",
			inode->i_private, 
			file->private_data
		  );

	// success
	return 0;
}

static int devone_close(struct inode *inode, struct file *file)
{
	printk("%s: major %d, minor %d (pid %d) \n", __func__,
			imajor(inode),
			iminor(inode),
			current->pid
		  );

	inode->i_private = inode;
	file->private_data = file;

	printk("  i_private=%p private_data=%p\n",
			inode->i_private, 
			file->private_data
		  );

	// success
	return 0;
}

ssize_t devone_read(struct file *filp, char __user * buf, size_t count, loff_t * f_pos)
{
	struct devone_data *p = filp->private_data;
	int i;
	unsigned char val;
	int retval;


	return 0;
}

ssize_t devone_write(struct file *filp, const char __user *buf, size_t count, loff_t * f_pos)
{
	struct devone_data *p = filp->private_data;

	unsigned char val;
	int retval = 0;

	printk("%s: count %ld fpos %lld\n", __func__, count, *f_pos);

	if (count >= 1) {
		if (copy_from_user(&val, &buf[0], 1)) {
			retval = -EFAULT;
			goto out;
		}

		write_lock(&p->lock);
		p->val = val;
		write_unlock(&p->lock);
		retval = count;
	}

out:
	return (retval);
}
struct file_operations devone_fops = {
	.open = devone_open,
	.release = devone_close,
	.read = devone_read,
	.write = devone_write,
};

static int devone_init(void)
{
	int major;
	int ret = 0;

	major = register_chrdev(devone_major, DRIVER_NAME, &devone_fops);
	if ((devone_major > 0 && major != 0) || 
			(devone_major == 0 && major < 0) ||
			major < 0) {
		printk("%s driver registration error\n", DRIVER_NAME);
		ret = major;
		goto error;
	}
	if (devone_major == 0) { /* dynamic allocation */
		devone_major = major;
	}

error:
	return (ret);
}


static void devone_exit(void)
{
	unregister_chrdev(devone_major, DRIVER_NAME);

	printk("%s drive removed.\n", DRIVER_NAME);
}

module_init(devone_init);
module_exit(devone_exit);

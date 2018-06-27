#include <linux/module.h>
#include <linux/init.h>

MODULE_LICENSE("Dual, Dual BSD/GPL");

extern void sub(void);

static int hello_init(void)
{
	printk(KERN_ALERT "driver loaded, helle!\n");
	sub();
	return 0;
}

static void hello_exit(void)
{
	printk(KERN_ALERT "driver unloaded, bye!\n");
}

module_init(hello_init);
module_exit(hello_exit);

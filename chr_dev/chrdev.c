#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_DESCRIPTION("Small Test Module");
MODULE_AUTHOR("kztomita");
MODULE_LICENSE("GPL");

/* Moduleがロードされた時に呼び出される初期化ルーチン */
static int smallmod_init_module(void)
{
	int error = 0;

	/*
	 *          * 初期化処理を行なう
	 *                   */
	printk("smallmod is loaded.\n");

	/* エラーの場合 */
	if (error)
		return  -ENODEV;

	return 0;
}

/* Moduleがアンロードされたる時に呼び出される後処理 */
static void smallmod_cleanup_module(void)
{
	printk("smallmod is unloaded.\n");
}

module_init(smallmod_init_module);
module_exit(smallmod_cleanup_module);

#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("Dual BSD/GPL");

struct sample_data {
	spinlock_t lock;
	struct file *file;
	struct list_head list;
	wait_queue_head_t wait;
	int no;
}

struct sample_data head;

void free_struct(void)
{
	struct list_head *listptr;
	struct sample_data *entry;

	list_for_each(listptr, &head.list) {
		entry = list_entry(listptr, struct sample_data, list);
		printk("Free: no=%d (list %p, next %p, prev %p)\n",
				entry->no, &entry->list, entry->list.next, entry->list.prev);
		kfree(entry);
	}
}

void show_stract(void)
{

}

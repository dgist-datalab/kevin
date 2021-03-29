#include <linux/module.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/backing-dev.h>
#include <linux/blk-mq.h>
#include <linux/spinlock.h>

#include "db.h"
#include "lightfs.h"


// Lock must be held and freed before and after push()
int lightfs_queue_push(struct lightfs_queue *q, void *data) {
	int id;
	unsigned long irqflags;
	struct lightfs_queue_item *item; 

	while(down_interruptible(&q->slots) == -EINTR) {
		//pr_info("interrupt - 1\n");
	}
	spin_lock_irqsave(&q->queue_spin, irqflags);

	item = list_first_entry(&q->free_tag_list, struct lightfs_queue_item, tag);
	list_move_tail(&item->tag, &q->processing_tag_list);
	id = item->id;
	item->data = data;

	spin_unlock_irqrestore(&q->queue_spin, irqflags);

	up(&q->items);	/* Announce available item */
	return id;
}

// Queue is locked until pop
void *lightfs_queue_peek(struct lightfs_queue *q) {
	struct lightfs_queue_item *item;
	unsigned long irqflags;

	while(down_interruptible(&q->items) == -EINTR) {}	/* Wait for available item */

	/* Lock the buffer */
	spin_lock_irqsave(&q->queue_spin, irqflags);

	item = list_first_entry(&q->processing_tag_list, struct lightfs_queue_item, tag);
	list_del(&item->tag);

	spin_unlock_irqrestore(&q->queue_spin, irqflags);

	return item->data;
}

void lightfs_queue_pop(struct lightfs_queue *q, int id) {
	unsigned long irqflags;
	struct lightfs_queue_item *item;

	spin_lock_irqsave(&q->queue_spin, irqflags);

	item = (q->item_arr)[id];
	
	list_add_tail(&item->tag, &q->free_tag_list);

	spin_unlock_irqrestore(&q->queue_spin, irqflags);

	up(&q->slots);	/* Announce available slot */
}

void *lightfs_queue_peek_and_pop(struct lightfs_queue *q) {
	struct lightfs_queue_item *item;
	unsigned long irqflags;

	while(down_interruptible(&q->items) == -EINTR) {}	/* Wait for available item */

	/* Lock the buffer */
	spin_lock_irqsave(&q->queue_spin, irqflags);

	item = list_first_entry(&q->processing_tag_list, struct lightfs_queue_item, tag);
	list_del(&item->tag);

	list_add_tail(&item->tag, &q->free_tag_list);

	spin_unlock_irqrestore(&q->queue_spin, irqflags);

	up(&q->slots);	/* Announce available slot */

	return item->data;
}

bool lightfs_queue_is_empty(struct lightfs_queue *q) {
	bool ret;
	unsigned long irqflags;
	spin_lock_irqsave(&q->queue_spin, irqflags);
	ret = list_empty(&q->processing_tag_list);
	spin_unlock_irqrestore(&q->queue_spin, irqflags);
	return ret;
}




void lightfs_queue_init(struct lightfs_queue **q, int cnt) {
	int i;
	struct lightfs_queue_item *item;
	struct lightfs_queue *_q;
	*q = kzalloc(sizeof(struct lightfs_queue), GFP_KERNEL);
	
	_q = *q;

	INIT_LIST_HEAD(&_q->free_tag_list);
	INIT_LIST_HEAD(&_q->processing_tag_list);
	spin_lock_init(&_q->queue_spin);

	_q->item_arr = vzalloc(sizeof(struct lightfs_queue_item *) * cnt);
	for (i = 0; i < cnt; i++) {
		item = kzalloc(sizeof(struct lightfs_queue_item), GFP_KERNEL);
		item->id = i;
		(_q->item_arr)[i] = item;
		INIT_LIST_HEAD(&item->tag);
		list_add_tail(&item->tag, &_q->free_tag_list);
	}

	sema_init(&_q->slots, cnt);	/* Initially, buf has n empty slots */
	sema_init(&_q->items, 0);	/* Initially, buf has zero data items */
	_q->cap = cnt;
}


void lightfs_queue_exit(struct lightfs_queue *q) {
	int i;
	struct lightfs_queue_item *item;
	for (i = 0; i < q->cap; i++) {
		item = (q->item_arr)[i];
		kfree(item);
	}
	vfree(q->item_arr);
	kfree(q);
}

#ifndef __LIGHTFS_QUEUE__
#define __LIGHTFS_QUEUE__

#include <linux/spinlock.h>
#include <linux/list.h>

int lightfs_queue_push(struct lightfs_queue *q, void *data); 
void *lightfs_queue_peek(struct lightfs_queue *q); 
void lightfs_queue_pop(struct lightfs_queue *q, int id);
void *lightfs_queue_peek_and_pop(struct lightfs_queue *q);
bool lightfs_queue_is_empty(struct lightfs_queue *q);
void lightfs_queue_init(struct lightfs_queue **q, int cnt);
void lightfs_queue_exit(struct lightfs_queue *q);

#endif

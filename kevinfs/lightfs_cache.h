#ifndef __LIGHTFS_CACHE_H__
#define __LIGHTFS_CACHE_H__

#include "db.h"
#include "lightfs_fs.h"
#include <linux/hashtable.h>

struct dcache_entry {
	bool is_full;
	struct rb_root rb_root;
	spinlock_t lock;
	uint32_t child;
	uint32_t e_child;
};

struct ht_lock_item {
	//spinlock_t lock;
	struct rw_semaphore lock;
	struct hlist_node hnode;
};

struct ht_cache_item {
	DBT key, value;
	bool is_weak_del;
	bool is_evicted;
	uint32_t fp;
	struct hlist_node hnode;
	struct rb_node rb_node;
	struct dcache_entry *dcache; 
	struct ht_cache_item *parent;
};

struct dcache_dbc_wrap {
	struct ht_cache_item *node;
	DBT *left, *right;
	DBC dbc;
};

int lightfs_cache_create(DB **, DB_ENV *, uint32_t);

#endif

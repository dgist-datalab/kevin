#include <linux/crc32.h>
#include "lightfs.h"
#include "lightfs_txn_hdlr.h"
#include "rbtreekv.h"
#include "lightfs_cache.h"

#define FSEED 0
#define SSEED 17

DEFINE_HASHTABLE (lightfs_ht_cache, HASHTABLE_BITS);
DEFINE_HASHTABLE (lightfs_ht_lock, HASHTABLE_BITS);

static struct kmem_cache *dcache_entry_cachep;
static struct kmem_cache *ht_cache_item_cachep;
static struct kmem_cache *meta_cachep;

static int _dbt_copy(DBT *to, const DBT *from) {
	memcpy(to, from, sizeof(DBT));
	to->data = kmalloc(from->size, GFP_ATOMIC);
	if (to->data == NULL) {
		return -ENOMEM;
	}
	memcpy(to->data, from->data, from->size);
	return 0;
}
static int _dbt_no_alloc_copy(DBT *to, const DBT *from) {
	memcpy(to->data, from->data, from->size);
	return 0;
}

static int _dbt_copy_meta(DBT *to, const DBT *from) {
	memcpy(to, from, sizeof(DBT));
	to->data = kmem_cache_alloc(meta_cachep, GFP_ATOMIC);
	if (to->data == NULL) {
		return -ENOMEM;
	}
	memcpy(to->data, from->data, from->size);
	return 0;
}


static int lightfs_keycmp(char *akey, uint16_t alen, char *bkey, uint16_t blen)
{
	int r;
	if (alen < blen) {
		r = memcmp(akey, bkey, alen);
		if (r)
			return r;
		return -1;
	} else if (alen > blen) {
		r = memcmp(akey, bkey, blen);
		if (r)
			return r;
		return 1;
	}
	// alen == blen
	return memcmp(akey, bkey, alen);
}


static inline uint32_t lightfs_ht_func (int seed, char *buf, uint32_t len) 
{
	return crc32(seed, buf, len);
}

static int lightfs_ht_cache_open (DB *db, DB_TXN *txn, const char *file, const char *database, DBTYPE type, uint32_t flag, int mode)
{
	return 0;
}

static inline void lightfs_dcache_entry_init(struct ht_cache_item *dir_ht_item) {
	dir_ht_item->dcache = kmem_cache_alloc(dcache_entry_cachep, GFP_ATOMIC);
	dir_ht_item->dcache->is_full = true;
	dir_ht_item->dcache->rb_root = RB_ROOT;
	dir_ht_item->dcache->child = 0;
	dir_ht_item->dcache->e_child = 0;

	spin_lock_init(&dir_ht_item->dcache->lock);
}

static inline void lightfs_dcache_entry_free (struct ht_cache_item *dir_ht_item) {
	if (dir_ht_item->dcache) {
		//free_rb_tree(dir_ht_item->dcache->rb_root.rb_node);
		kmem_cache_free(dcache_entry_cachep, dir_ht_item->dcache);
		dir_ht_item->dcache = NULL;
	}
}


static inline void lightfs_ht_cache_item_init (struct ht_cache_item **ht_item, DBT *key, DBT *value) {
	*ht_item = kmem_cache_alloc(ht_cache_item_cachep, GFP_ATOMIC);
	_dbt_copy(&((*ht_item)->key), key);
	_dbt_copy_meta(&((*ht_item)->value), value);
	INIT_HLIST_NODE(&(*ht_item)->hnode);
	(*ht_item)->dcache = NULL;
	(*ht_item)->parent = NULL;
}

static inline void lightfs_ht_cache_item_free (struct ht_cache_item *ht_item) {
	kfree(ht_item->key.data);
	//if (cache_item->value.data)
	kmem_cache_free(meta_cachep, ht_item->value.data);
	kmem_cache_free(ht_cache_item_cachep, ht_item);
}

static int lightfs_dcache_insert (struct ht_cache_item *dir_ht_item, struct ht_cache_item *node)
{
	struct rb_node **new = &(dir_ht_item->dcache->rb_root.rb_node), *parent = NULL;


	spin_lock(&(dir_ht_item->dcache->lock));

	while (*new) {
		struct ht_cache_item *this = container_of(*new, struct ht_cache_item, rb_node);
		int result = lightfs_keycmp(node->key.data, node->key.size, this->key.data, this->key.size);
		parent = *new;

		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else {

			spin_unlock(&(dir_ht_item->dcache->lock));
			return -1;
		}
	}

	rb_link_node(&node->rb_node, parent, new);
	rb_insert_color(&node->rb_node, &(dir_ht_item->dcache->rb_root));
	spin_unlock(&(dir_ht_item->dcache->lock));

	return 0;
}


static int lightfs_dcache_invalidate (struct ht_cache_item *node)
{
	struct ht_cache_item *dir_cache_item;
	dir_cache_item = node->parent;

	//BUG_ON(dir_cache_item == NULL);

	if (!dir_cache_item) {
		//print_key(__func__, node->key.data, node->key.size);
		return DB_NOTFOUND;
	}

	spin_lock(&(dir_cache_item->dcache->lock));

	dir_cache_item->dcache->is_full = true; // TODO: fix me
	dir_cache_item->dcache->e_child++; // TODO: fix me
	//pr_info("child %d, e_child %d\n", dir_cache_item->dcache->child, dir_cache_item->dcache->e_child);
#ifdef GROUP_EVICTION
	if (dir_cache_item->dcache->e_child == dir_cache_item->dcache->child) {
		//pr_info("evict!!\n");
		lightfs_ht_cache_group_eviction(&(dir_cache_item->key));
	}
#endif
	spin_unlock(&(dir_cache_item->dcache->lock));

	return 0;
}

static int lightfs_dcache_del (struct ht_cache_item *node)
{
	struct dcache_entry *dcache = node->parent->dcache;
	spin_lock(&dcache->lock);
	rb_erase(&node->rb_node, &(dcache->rb_root));
	spin_unlock(&dcache->lock);
	return 0;
}

static int lightfs_ht_cache_get (DB *db, DB_TXN *txn, DBT *key, DBT *value, enum lightfs_req_type type)
{
	struct ht_lock_item *ht_item;
	struct ht_cache_item *cache_item;
	uint32_t hkey = lightfs_ht_func(FSEED, key->data, key->size);
	uint32_t fp = lightfs_ht_func(SSEED, key->data, key->size);

	hash_for_each_possible(lightfs_ht_lock, ht_item, hnode, hkey) {
		//print_key(__func__, key->data, key->size);
		//spin_lock_bh(&ht_item->lock);
		//spin_lock(&ht_item->lock);
		down_read(&ht_item->lock);
		hash_for_each_possible(lightfs_ht_cache, cache_item, hnode, hkey) {
			if (cache_item->fp == fp && !lightfs_keycmp(cache_item->key.data, cache_item->key.size, key->data, key->size)) {
				if (cache_item->is_weak_del && cache_item->is_evicted) {
					memcpy(value->data, cache_item->value.data, value->size);
					cache_item->is_weak_del = cache_item->is_evicted = 0;
					//spin_unlock_bh(&ht_item->lock);
					//spin_unlock(&ht_item->lock);
					up_read(&ht_item->lock);
					return DB_FOUND_FREE;
				} else {
					memcpy(value->data, cache_item->value.data, value->size);
					//spin_unlock_bh(&ht_item->lock);
					//spin_unlock(&ht_item->lock);
					up_read(&ht_item->lock);
					return 0;
				}
			}
		}
		up_read(&ht_item->lock);
		//spin_unlock_bh(&ht_item->lock);
		//spin_unlock(&ht_item->lock);
	}
	return DB_NOTFOUND;
}

int lightfs_ht_cache_group_eviction (DBT *key)
{
	struct ht_lock_item *ht_item;
	struct ht_cache_item *cache_item, *child_cache_item = NULL;
	uint32_t hkey = lightfs_ht_func(FSEED, key->data, key->size);
	uint32_t fp = lightfs_ht_func(SSEED, key->data, key->size);
	struct rb_node *rb_node = NULL;
	int ret = 0;
	int child = 0;

	hash_for_each_possible(lightfs_ht_lock, ht_item, hnode, hkey) {
		down_write(&ht_item->lock);
		hash_for_each_possible(lightfs_ht_cache, cache_item, hnode, hkey) {
			if (cache_item->fp == fp && !lightfs_keycmp(cache_item->key.data, cache_item->key.size, key->data, key->size)) {
				BUG_ON (cache_item->dcache == NULL);
				rb_node = rb_first(&(cache_item->dcache->rb_root));
				if (rb_node) { // empty dir
					child_cache_item = container_of(rb_node, struct ht_cache_item, rb_node);	
				} else {
					child_cache_item = NULL;
				}
				break;
			}
		}
		//the item which is be inserted newly
		up_write(&ht_item->lock);
	}
	if (child_cache_item) {
		DB_TXN *txn;
		if (!child_cache_item->key.size) {
			return 0;
		}
repeat:
		TXN_GOTO_LABEL(retry);
		lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
		while (rb_node) {
			if (++child > GROUP_EVICTION_TRESHOLD) {
				break;
			}
			child_cache_item = container_of(rb_node, struct ht_cache_item, rb_node);
			//print_key(__func__, child_cache_item->key.data, child_cache_item->key.size);
			ret = lightfs_bstore_meta_put_tmp(NULL, &child_cache_item->key, txn, child_cache_item->value.data, NULL, 0);
			rb_node = rb_next(rb_node);
		}
		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
		} else {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			COMMIT_JUMP_ON_CONFLICT(ret, retry);
		}
		if (child > GROUP_EVICTION_TRESHOLD) {
			child = 0;
			goto repeat;
		}
	}
	return 0;
}


static int lightfs_ht_cache_put (DB *db, DB_TXN *txn, DBT *key, DBT *value, enum lightfs_req_type type, struct inode *dir_inode, bool is_dir)
{
	struct ht_lock_item *ht_item;
	struct ht_cache_item *cache_item = NULL, *dir_cache_item;
	DBT *dir_key = NULL;
	uint32_t dir_hkey, dir_fp;
	struct lightfs_inode *dir_f_inode;
	uint32_t hkey = lightfs_ht_func(FSEED, key->data, key->size);
	uint32_t fp = lightfs_ht_func(SSEED, key->data, key->size);
	int fp_cnt = 0, keycmp_cnt = 0;


	hash_for_each_possible(lightfs_ht_lock, ht_item, hnode, hkey) {
		//print_key(__func__, key->data, key->size);
		//lightfs_error(__func__, "key_size: %d, hkey: %d, fp: %d\n", key->size, hkey, fp);
		//spin_lock_bh(&ht_item->lock);
		//spin_lock(&ht_item->lock);
		down_write(&ht_item->lock);
		hash_for_each_possible(lightfs_ht_cache, cache_item, hnode, hkey) {
			if (cache_item->fp == fp && !lightfs_keycmp(cache_item->key.data, cache_item->key.size, key->data, key->size)) {
				if (cache_item->is_weak_del) {
					//_dbt_copy_meta(&cache_item->value, value);
					//_dbt_copy_meta(&cache_item->value, value);
					_dbt_no_alloc_copy(&cache_item->value, value);
				} else {
					_dbt_no_alloc_copy(&cache_item->value, value);
				}
				cache_item->is_weak_del = cache_item->is_evicted = 0;
				//spin_unlock_bh(&ht_item->lock);
				//spin_unlock(&ht_item->lock);
				up_write(&ht_item->lock);
				return 0;
			} else {
				if (cache_item->fp == fp)
					fp_cnt++;
				else {
					keycmp_cnt++;
				}
			}
		}
		//the item which is be inserted newly
		lightfs_ht_cache_item_init(&cache_item, key, value);
		cache_item->fp = fp;
		cache_item->is_weak_del = 0;
		cache_item->is_evicted = 0;
		if (is_dir) {
			lightfs_dcache_entry_init(cache_item);
		}
		if (dir_inode) {
			dir_f_inode = LIGHTFS_I(dir_inode);
			dir_key = &(dir_f_inode->meta_dbt);
			//print_key(__func__, dir_key->data, dir_key->size);
		} else {
			dir_key = NULL;
		}
		//spin_unlock_bh(&ht_item->lock);
		//spin_unlock(&ht_item->lock);
		hash_add(lightfs_ht_cache, &(cache_item->hnode), hkey);
		up_write(&ht_item->lock);
	}
	if (dir_key) {
		dir_hkey = lightfs_ht_func(FSEED, dir_key->data, dir_key->size);
		dir_fp = lightfs_ht_func(SSEED, dir_key->data, dir_key->size);
		//lightfs_error(__func__, "DIR key_size: %d, hkey: %d, fp: %d\n", key->size, dir_hkey, dir_fp);
		hash_for_each_possible(lightfs_ht_cache, dir_cache_item, hnode, dir_hkey) {
			if (dir_cache_item->fp == dir_fp && !lightfs_keycmp(dir_cache_item->key.data, dir_cache_item->key.size, dir_key->data, dir_key->size)) {
				BUG_ON(dir_cache_item->dcache == NULL);
				lightfs_dcache_insert(dir_cache_item, cache_item);
				dir_cache_item->dcache->child++;
				//pr_info("insert child %d, e_child %d\n", dir_cache_item->dcache->child, dir_cache_item->dcache->e_child);
				// directory item
				cache_item->parent = dir_cache_item;
			}
		}
	}
	return 0;
}

static int lightfs_ht_cache_del (DB *db , DB_TXN *txn, DBT *key, enum lightfs_req_type type, bool is_dir)
{
	struct ht_lock_item *ht_item;
	struct ht_cache_item *cache_item = NULL;
	uint32_t hkey = lightfs_ht_func(FSEED, key->data, key->size);
	uint32_t fp = lightfs_ht_func(SSEED, key->data, key->size);
	volatile bool found = 0;

	hash_for_each_possible(lightfs_ht_lock, ht_item, hnode, hkey) {
		//spin_lock_bh(&ht_item->lock);
		//spin_lock(&ht_item->lock);
		down_write(&ht_item->lock);
		hash_for_each_possible(lightfs_ht_cache, cache_item, hnode, hkey) {
			if (cache_item->fp == fp && !lightfs_keycmp(cache_item->key.data, cache_item->key.size, key->data, key->size)) {
				found = 1;
				break;
			}
		}
		if (found) {
			hash_del(&cache_item->hnode);
		}
		//spin_unlock_bh(&ht_item->lock);
		//spin_unlock(&ht_item->lock);
		up_write(&ht_item->lock);
	}

	if (!found)
		return DB_NOTFOUND;


			lightfs_dcache_del(cache_item);

	if (is_dir) {
		BUG_ON(cache_item->parent->dcache == NULL);
		lightfs_dcache_entry_free(cache_item);
	}
	lightfs_ht_cache_item_free(cache_item);

	return 0;
}

static int lightfs_ht_cache_weak_del (DB *db , DB_TXN *txn, DBT *key, enum lightfs_req_type type)
{
	struct ht_lock_item *ht_item;
	struct ht_cache_item *cache_item;
	uint32_t hkey = lightfs_ht_func(FSEED, key->data, key->size);
	uint32_t fp = lightfs_ht_func(SSEED, key->data, key->size);
	volatile bool found = 0;

	hash_for_each_possible(lightfs_ht_lock, ht_item, hnode, hkey) {
		//spin_lock_bh(&ht_item->lock);
		//spin_lock(&ht_item->lock);
		down_write(&ht_item->lock);
		hash_for_each_possible(lightfs_ht_cache, cache_item, hnode, hkey) {
			if (cache_item->fp == fp && !lightfs_keycmp(cache_item->key.data, cache_item->key.size, key->data, key->size)) {
				found = 1;
				break;
			}
		}
		if (found) {
			up_write(&ht_item->lock);
			lightfs_dcache_invalidate(cache_item);
			down_write(&ht_item->lock);
			cache_item->is_weak_del = cache_item->is_evicted = 1;
		}
		//spin_unlock_bh(&ht_item->lock);
		//spin_unlock(&ht_item->lock);
		up_write(&ht_item->lock);
	}

	if (!found)
		return DB_NOTFOUND;

	return 0;
}



static int lightfs_ht_cache_close(DB *db, uint32_t flag)
{
#ifdef RB_CACHE
	db_cache_close(db, flag);
	kfree(db);
#else
	int i;
	struct ht_lock_item *ht_item;
	struct ht_cache_item *cache_item;
	struct hlist_node *hnode;
	for (i = 0; i < (1 << HASHTABLE_BITS); i++) {
		ht_item = hlist_entry(lightfs_ht_lock[i].first, struct ht_lock_item, hnode);
		hash_del(&ht_item->hnode);
		kfree(ht_item);
	}
	for (i = 0; i < (1 << HASHTABLE_BITS); i++) {
		hlist_for_each_entry_safe(cache_item, hnode, &lightfs_ht_cache[i], hnode) {
			hlist_del(&cache_item->hnode);
			lightfs_dcache_entry_free(cache_item);
			lightfs_ht_cache_item_free(cache_item);
		}
	}
	kmem_cache_destroy(meta_cachep);
	kmem_cache_destroy(ht_cache_item_cachep);
	kmem_cache_destroy(dcache_entry_cachep);
	kfree(db);
#endif

	return 0;
}

static int lightfs_dcache_close(DBC *c) {
	struct dcache_dbc_wrap *wrap = container_of(c, struct dcache_dbc_wrap, dbc);
	kfree(wrap);
	return 0;
}

static int lightfs_dcache_c_get(DBC *c, DBT *key, DBT *value, uint32_t flags)
{
	struct dcache_dbc_wrap *wrap;
	struct ht_cache_item *cache_item = NULL;
	struct rb_node *node;
	uint32_t hkey;
	uint32_t fp;
	//print_key(__func__, key->data, key->size);
	if (flags == DB_SET_RANGE) {
		struct ht_lock_item *ht_item;
		hkey = lightfs_ht_func(FSEED, key->data, key->size);
		fp = lightfs_ht_func(SSEED, key->data, key->size);
		wrap = container_of(c, struct dcache_dbc_wrap, dbc);
		//lightfs_error(__func__, "key_size: %d, hkey: %d, fp: %d\n", key->size, hkey, fp);
		if (wrap->node) {
			cache_item = wrap->node;
			memcpy(key->data, cache_item->key.data, cache_item->key.size);
			key->size = cache_item->key.size;
			memcpy(value->data, cache_item->value.data, value->size);
			return 0;
		}
		hash_for_each_possible(lightfs_ht_lock, ht_item, hnode, hkey) {
			down_read(&ht_item->lock);

			hash_for_each_possible(lightfs_ht_cache, cache_item, hnode, hkey) {
				if (cache_item->fp == fp && !lightfs_keycmp(cache_item->key.data, cache_item->key.size, key->data, key->size)) {
					//lightfs_error(__func__, "found");
					break;
				}
			}
			up_read(&ht_item->lock);
			if (cache_item) { // found directory
				struct rb_node *rb_node;
				spinlock_t *lock = &(cache_item->dcache->lock);
				spin_lock(lock);
				rb_node = rb_first(&(cache_item->dcache->rb_root));
				if (rb_node) {
					cache_item = container_of(rb_node, struct ht_cache_item, rb_node);
				} else {
					if (cache_item->dcache->is_full) {
						spin_unlock(lock);
						return DB_NOTFOUND_DCACHE_FULL;
					} else {
       	                                 	spin_unlock(lock);
						return DB_NOTFOUND_DCACHE_FULL;
					}
				}
				spin_unlock(lock);
			} else {
				return DB_NOTFOUND_DCACHE_FULL;
				BUG_ON(1);
			}
		}
		wrap->node = cache_item;
		memcpy(key->data, cache_item->key.data, cache_item->key.size);
		key->size = cache_item->key.size;
		memcpy(value->data, cache_item->value.data, value->size);
		//key->data = cache_item->key.data;
		//key->size = cache_item->key.size;
		//value->data = cache_item->value.data;
		return 0;
	} 


	wrap = container_of(c, struct dcache_dbc_wrap, dbc);
	if (wrap->node) {
		cache_item = wrap->node->parent;
	} else {
		return DB_NOTFOUND_DCACHE_FULL;
		if (flags != DB_SET_RANGE)
			BUG_ON(1);
		print_key(__func__, key->data, key->size);
	}

	spin_lock(&(cache_item->dcache->lock));
	node = &(wrap->node->rb_node);
	node = rb_next(node);
	if (node == NULL) {
		wrap->node = NULL;
		if (cache_item->dcache->is_full) {
			spin_unlock(&(cache_item->dcache->lock));
			return DB_NOTFOUND_DCACHE_FULL;
		} else {
			spin_unlock(&(cache_item->dcache->lock));
			return DB_NOTFOUND_DCACHE_FULL;
		}

	}
	spin_unlock(&(cache_item->dcache->lock));

	if (wrap->right != NULL) {
		struct ht_cache_item *ht_item = container_of(node, struct ht_cache_item, rb_node);
		if (lightfs_keycmp(ht_item->key.data, ht_item->key.size, wrap->right->data, wrap->right->size) > 0) {
			wrap->node = NULL;
			if (cache_item->dcache->is_full)
				return DB_NOTFOUND_DCACHE_FULL;
			else
				return DB_NOTFOUND_DCACHE_FULL;

		}
	}
	wrap->node = container_of(node, struct ht_cache_item, rb_node);
	memcpy(key->data, wrap->node->key.data, wrap->node->key.size);
	key->size = wrap->node->key.size;
	memcpy(value->data, wrap->node->value.data, value->size);
	
	return 0;
}

int lightfs_dcache_cursor(DB *db, DB_TXN *txnid, DBC **cursorp, uint32_t flags)
{
	struct dcache_dbc_wrap *wrap = kmalloc(sizeof(struct dcache_dbc_wrap), GFP_NOIO);
	if (wrap == NULL) {
		return -ENOMEM;
	}

	wrap->node = NULL;
	*cursorp = &wrap->dbc;
	wrap->left = NULL;
	wrap->right = NULL;

	(*cursorp)->dbp = db;
	(*cursorp)->c_get = lightfs_dcache_c_get;
	(*cursorp)->c_close = lightfs_dcache_close;

	return 0;
}



int lightfs_cache_create(DB **db, DB_ENV *env, uint32_t flags)
{
#ifdef RB_CACHE
	*db = kmalloc(sizeof(DB), GFP_NOIO);
	if (*db == NULL) {
		return -ENOMEM;
	}
	db_cache_create(db, env, flags);
	BUG_ON((*db)->i == NULL);
	(*db)->dbenv = env;

	(*db)->open = lightfs_cache_open;
	(*db)->close = lightfs_cache_close;
	(*db)->get = lightfs_cache_get;
	(*db)->put = lightfs_cache_put;
	(*db)->del = lightfs_cache_del;
	(*db)->weak_del = lightfs_cache_weak_del;
#else
	int i;
	struct ht_lock_item *ht_item;

	*db = kmalloc(sizeof(DB), GFP_NOIO);
	if (*db == NULL) {
		return -ENOMEM;
	}
	(*db)->dbenv = env;

	(*db)->open = lightfs_ht_cache_open;
	(*db)->close = lightfs_ht_cache_close;
	(*db)->cache_get = lightfs_ht_cache_get;
	(*db)->cache_put = lightfs_ht_cache_put;
	(*db)->cache_del = lightfs_ht_cache_del;
	(*db)->cache_weak_del = lightfs_ht_cache_weak_del;
	(*db)->cursor = lightfs_dcache_cursor;


	hash_init(lightfs_ht_cache);
	hash_init(lightfs_ht_lock);
	for (i = 0; i < (1 << HASHTABLE_BITS); i++) {
		ht_item = kmalloc(sizeof(struct ht_lock_item), GFP_NOIO);
		//spin_lock_init(&ht_item->lock);
		init_rwsem(&ht_item->lock);
		INIT_HLIST_NODE(&ht_item->hnode);
		hlist_add_head(&ht_item->hnode, &lightfs_ht_lock[i]);
	}

	dcache_entry_cachep = kmem_cache_create("lightfs_dcache_entry_cachep", sizeof(struct dcache_entry), 0, KMEM_CACHE_FLAG, NULL);
	if (!dcache_entry_cachep)
		kmem_cache_destroy(dcache_entry_cachep);


	ht_cache_item_cachep = kmem_cache_create("lightfs_ht_cachep", sizeof(struct ht_cache_item), 0, KMEM_CACHE_FLAG, NULL);
	if (!ht_cache_item_cachep)
		kmem_cache_destroy(ht_cache_item_cachep);

	meta_cachep = kmem_cache_create("lightfs_meta_cachep", INODE_SIZE, 0, KMEM_CACHE_FLAG, NULL);
	if (!meta_cachep)
		kmem_cache_destroy(meta_cachep);

#endif

	return 0;
}

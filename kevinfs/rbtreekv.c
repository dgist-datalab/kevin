/* -*- mode: C++; c-basic-offset: 8; indent-tabs-mode: t -*- */
// vim: set tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab:
#ifndef RBTREE_KV_H
#define RBTREE_KV_H

#include "db.h"
#include "lightfs_fs.h"
#include "lightfs.h"

#ifdef RB_LOCK
static struct mutex rb_lock;
#endif
#ifdef RB_SPIN
static spinlock_t rb_spin; 
#endif
static struct rw_semaphore rb_sem;
static struct kmem_cache *rb_kv_cachep;
static struct kmem_cache *rb_meta_cachep;

/*
 * Create one db as a rbtree. One db_env has a list of rbtrees, each
 * has its own rbtree struct;
 */
struct rb_kv_node {
	DBT key, val;
	struct rb_node node;
};

struct rb_cache_kv_node {
	DBT key, value;
	int is_updated;
	struct rb_node node;
};


struct __lightfs_db_internal {
	/* maybe we need a point back to db here */
	struct rb_root kv;
	struct list_head rbtree_list;
};
struct __lightfs_db_env_internal {
	struct list_head rbtree_list;
	int (*update_cb)(DB *, const DBT *key, const DBT *old_val, const DBT *extra, void (*set_val)(const DBT *new_val, void *set_extra), void *set_extra);
	int (*bt_compare)(DB *, const DBT *, const DBT *);
};
/*
 * __lightfs_dbc doesn't offer us anything like xx_internal to store our own data,
 * thus I try to wrap things into bigger data structure
 */
struct __lightfs_dbc_wrap {
	struct rb_kv_node *node;
	DBT *left, *right;
	DBC  dbc;
};

static int dbt_alloc_and_copy(DBT **to, const DBT *from)
{
	*to = kmalloc(sizeof(DBT), GFP_KERNEL);
	if (*to == NULL) {
		return -ENOMEM;
	}
	memcpy(*to, from, sizeof(DBT));
	(*to)->data = kmalloc(from->size, GFP_KERNEL);
	if ((*to)->data == NULL) {
		kfree(*to);
		*to = NULL;
		return -ENOMEM;
	}
	memcpy((*to)->data, from->data, from->size);
	return 0;
}
static int _dbt_copy(DBT *to, const DBT *from) {
	memcpy(to, from, sizeof(DBT));
	to->data = kvmalloc(from->size, GFP_KERNEL);
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
	//to->data = kmalloc(from->size, GFP_NOIO);
	to->data = kmem_cache_alloc(rb_meta_cachep, GFP_KERNEL);
	if (to->data == NULL) {
		return -ENOMEM;
	}
	memcpy(to->data, from->data, from->size);
	return 0;
}

static int env_keycmp(DBT const *a, DBT const *b)
{
	int r;
	uint32_t alen, blen;
	alen = a->size;
	blen = b->size;
	if (alen < blen) {
		r = memcmp(a->data, b->data, alen);
		if (r)
			return r;
		return -1;
	} else if (alen > blen) {
		r = memcmp(a->data, b->data, blen);
		if (r)
			return r;
		return 1;
	}
	// alen == blen
	return memcmp(a->data, b->data, alen);
}



int db_env_set_default_bt_compare(DB_ENV *env, int (*bt_compare)(DB *, const DBT *, const DBT *))
{
	env->i->bt_compare = bt_compare;

	return 0;
}

int db_env_close(DB_ENV *env, uint32_t flag)
{
	kfree(env->i);
	return 0;
}

static struct rb_kv_node *find_val_with_key(DB *db, const DBT *key)
{
	struct rb_node *node = db->i->kv.rb_node;

	while (node) {
		struct rb_kv_node *tmp = container_of(node, struct rb_kv_node, node);
		int result;

		//result = db->dbenv->i->bt_compare(db, key, &(tmp->key));
		result = env_keycmp(key, &tmp->key);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else {
			return tmp;
		}
	}

	return NULL;
}

static struct rb_kv_node *find_val_with_key_ge(DB *db, const DBT *key)
{
	struct rb_node *node = db->i->kv.rb_node;

	while (node) {
		struct rb_kv_node *tmp = container_of(node, struct rb_kv_node, node);
		int result;

		//result = db->dbenv->i->bt_compare(db, key, &(tmp->key));
		result = env_keycmp(key, &tmp->key);
		if (result < 0) {
			/* node key is bigger */
			struct rb_kv_node *next = tmp;
			while ((node = rb_prev(node)) != NULL) {
				next = tmp;
				tmp = container_of(node, struct rb_kv_node, node);
				//result = db->dbenv->i->bt_compare(db, key, &(tmp->key));
				result = env_keycmp(key, &tmp->key);
				if (result == 0) {
					return tmp;
				} else if (result > 0) {
					return next;
				}
			}
			return tmp;
		}
		else if (result > 0)
			node = node->rb_right;
		else {
			return tmp;
		}
	}

	return NULL;
}

int db_get(DB *db, DB_TXN *txnid, DBT *key, DBT *data, uint32_t flags)
{
	struct rb_kv_node *node;
	//print_key(__func__, key->data, key->size); 

#ifdef RB_LOCK
	mutex_lock(&rb_lock);
#endif
	node = find_val_with_key(db, key);
	if (node == NULL) {
#ifdef RB_LOCK
		mutex_unlock(&rb_lock);
#endif
		//lightfs_error(__func__, "NOT FOUND\n");
		return DB_NOTFOUND;
	}
	memcpy(data->data, node->val.data, data->size);
#ifdef RB_LOCK
	mutex_unlock(&rb_lock);
#endif


	return node->val.size;
}

int db_del(DB *db, DB_TXN *txnid, DBT *key, uint32_t flags)
{
	struct rb_kv_node *node;
	//print_key(__func__, key->data, key->size); 
#ifdef RB_LOCK
	mutex_lock(&rb_lock);
#endif
	node = find_val_with_key(db, key);
	if (node == NULL) {
#ifdef RB_LOCK
		mutex_unlock(&rb_lock);
#endif

		//lightfs_error(__func__, "NOT FOUND\n");
		return DB_NOTFOUND;
	}
	rb_erase(&node->node, &db->i->kv);
#ifdef RB_LOCK
	mutex_unlock(&rb_lock);
#endif
	kfree(node->key.data);
	kfree(node->val.data);
	kfree(node);

	return 0;
}

struct set_val_info {
	DB *db;
	const DBT *key;
	struct rb_kv_node *node;
};

static int rb_kv_insert(DB *db, struct rb_kv_node *node)
{
	struct rb_node **new = &(db->i->kv.rb_node), *parent = NULL;

	while (*new) {
		struct rb_kv_node *this = container_of(*new, struct rb_kv_node, node);
		//int result = db->dbenv->i->bt_compare(db, &node->key, &this->key);
		int result = env_keycmp(&node->key, &this->key);
		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return -1;
	}

	rb_link_node(&node->node, parent, new);
	rb_insert_color(&node->node, &(db->i->kv));

	return 0;
}

int db_update(DB *db, DB_TXN *txnid, const DBT *key, const DBT *value, loff_t offset, uint32_t flags)
{
	char *buf;
	struct rb_kv_node *node;
	//print_key(__func__, key->data, key->size); 
	int ret;
	DBT *val;
#ifdef RB_LOCK
	mutex_lock(&rb_lock);
#endif
	node = find_val_with_key(db, key);
	val = (node == NULL) ? NULL : &node->val;
	lightfs_error(__func__, "val: %p value->size:%llu, offset:%llu\n", val, value->ulen, offset);
	if (val) {
		buf = val->data;
		memcpy(buf + offset, value->data + offset, value->ulen);
		/*
		if (val->size > value->size) {
			val->size = value->size;
		}
		*/

	} else {
		node = kmalloc(sizeof(struct rb_kv_node), GFP_KERNEL);
		ret = _dbt_copy(&node->key, key);
		ret = _dbt_copy(&node->val, value);
		buf = node->val.data;
		memset(buf, 0, offset);
		memset(buf + offset + value->ulen, 0, 4096-offset-value->ulen);
		ret = rb_kv_insert(db, node);

	}

#ifdef RB_LOCK
	mutex_unlock(&rb_lock);
#endif
	//ret = db->dbenv->i->update_cb(db, key, val, extra, db_set_val, &info);

	return 0;
}


static void free_rb_tree(struct rb_node *node)
{
	struct rb_kv_node *kv_node = container_of(node, struct rb_kv_node, node);
	if (!node)
		return;
	if (node->rb_left)
	  free_rb_tree(node->rb_left);
	if (node->rb_right)
	  free_rb_tree(node->rb_right);
	kfree(kv_node->key.data);
	kfree(kv_node->val.data);
	kfree(kv_node);
}

int db_close(DB *db, uint32_t flag)
{
	free_rb_tree(db->i->kv.rb_node);
	kfree(db->i);
	return 0;
}



int db_put(DB *db, DB_TXN *txnid, DBT *key, DBT *data, uint32_t flags)
{
	/* flags are not used in lightfs */
	struct rb_kv_node *node;
	int ret;

	//print_key(__func__, key->data, key->size); 
#ifdef RB_LOCK
	mutex_lock(&rb_lock);
#endif
	node = find_val_with_key(db, key);
	if (node != NULL) {
		kfree(node->val.data);
		ret = _dbt_copy(&node->val, data);
		BUG_ON(ret != 0);
#ifdef RB_LOCK
		mutex_unlock(&rb_lock);
#endif
		return ret;
	}

	node = kmalloc(sizeof(struct rb_kv_node), GFP_KERNEL);

	ret = _dbt_copy(&node->key, key);
	BUG_ON(ret != 0);
	ret = _dbt_copy(&node->val, data);
	BUG_ON(ret != 0);
	ret = rb_kv_insert(db, node);
	BUG_ON(ret != 0);
#ifdef RB_LOCK
	mutex_unlock(&rb_lock);
#endif
	return ret;
}



static int dbc_c_getf_set_range(DBC *c, uint32_t flag, DBT *key, YDB_CALLBACK_FUNCTION f, void *extra)
{
	/* all things in lightfs use flag = 0, ignore it */
	struct __lightfs_dbc_wrap *wrap;
	struct rb_kv_node *node;
	//print_key(__func__, key->data, key->size); 
	node = find_val_with_key_ge(c->dbp, key);
#ifdef RB_LOCK
	mutex_lock(&rb_lock);
#endif
	if (node == NULL) {
#ifdef RB_LOCK
		mutex_unlock(&rb_lock);
#endif
		return DB_NOTFOUND;
	}
	wrap = container_of(c, struct __lightfs_dbc_wrap, dbc);
	wrap->node = node;
	f(&node->key, &node->val, extra);
#ifdef RB_LOCK
	mutex_unlock(&rb_lock);
#endif
	return 0;
}


static int dbc_c_getf_next(DBC *c, uint32_t flag, YDB_CALLBACK_FUNCTION f, void *extra)
{
	/* again, flag is ignored */
	struct __lightfs_dbc_wrap *wrap = container_of(c, struct __lightfs_dbc_wrap, dbc);
	struct rb_node *node = &(wrap->node->node);
#ifdef RB_LOCK
	mutex_lock(&rb_lock);
#endif
	node = rb_next(node); /* logical next */
	if (node == NULL) {
		wrap->node = NULL;
#ifdef RB_LOCK
		mutex_unlock(&rb_lock);
#endif
		//lightfs_error(__func__, "NOT FOUND\n");
		return DB_NOTFOUND;
	}
	/* since we only consider right bound in lightfs, simply do this */
	if (wrap->right != NULL) {
		struct rb_kv_node *kv_node = container_of(node, struct rb_kv_node, node);
		if (env_keycmp(&kv_node->key, wrap->right) > 0) {
			wrap->node = NULL;
			return DB_NOTFOUND;
		}
	}
	wrap->node = container_of(node, struct rb_kv_node, node);
	f(&wrap->node->key, &wrap->node->val, extra);
#ifdef RB_LOCK
	mutex_unlock(&rb_lock);
#endif

	return 0;
}

static void free_dbt(DBT *dbt) {
	kfree(dbt->data);
	kfree(dbt);
}

static int dbc_c_close(DBC *c) {
	struct __lightfs_dbc_wrap *wrap = container_of(c, struct __lightfs_dbc_wrap, dbc);
	if (wrap->left) {
		free_dbt(wrap->left);
	}
	if (wrap->right) {
		free_dbt(wrap->right);
	}
	kfree(wrap);
	return 0;
}

static int dbc_c_set_bounds(DBC *dbc, const DBT *left_key, const DBT *right_key, bool pre_acquire, int out_of_range_error)
{
	struct __lightfs_dbc_wrap *wrap = container_of(dbc, struct __lightfs_dbc_wrap, dbc);
	int ret;

	ret = dbt_alloc_and_copy(&wrap->left, left_key);
	BUG_ON(ret != 0);
	ret = dbt_alloc_and_copy(&wrap->right, right_key);
	BUG_ON(ret != 0);

	return 0;
}

static int dbc_c_getf_current(DBC *c, uint32_t flag, YDB_CALLBACK_FUNCTION f, void *extra)
{
	int r;
	struct __lightfs_dbc_wrap *wrap = container_of(c, struct __lightfs_dbc_wrap, dbc);

	BUG_ON(wrap->node == NULL);
	r= f(&wrap->node->key, &wrap->node->val, extra);

	return r;
}

static int dbc_c_get(DBC *c, DBT *key, DBT *value, uint32_t flags)
{
	struct __lightfs_dbc_wrap *wrap;
	//print_key(__func__, key->data, key->size); 
#ifdef RB_LOCK
	mutex_lock(&rb_lock);
#endif
	if (flags == DB_SET_RANGE) {
		struct rb_kv_node *node = find_val_with_key_ge(c->dbp, key);
		if (node == NULL) {
#ifdef RB_LOCK
			mutex_unlock(&rb_lock);
#endif
			return DB_NOTFOUND;
		}
		wrap = container_of(c, struct __lightfs_dbc_wrap, dbc);
		wrap->node = node;

		memcpy(key->data, node->key.data, node->key.size);
		key->size = node->key.size;
		memcpy(value->data, node->val.data, value->size);
	} else {
		struct rb_node *node;
		wrap = container_of(c, struct __lightfs_dbc_wrap, dbc);
		node = &(wrap->node->node);
		node = rb_next(node); /* logical next */
		if (node == NULL) {
			wrap->node = NULL;
#ifdef RB_LOCK
			mutex_unlock(&rb_lock);
#endif
			return DB_NOTFOUND;
		}
		if (wrap->right != NULL) {
			struct rb_kv_node *kv_node = container_of(node, struct rb_kv_node, node);
			if (env_keycmp(&kv_node->key, wrap->right) > 0) {
				wrap->node = NULL;
#ifdef RB_LOCK
				mutex_unlock(&rb_lock);
#endif
				return DB_NOTFOUND;
			}
		}
		wrap->node = container_of(node, struct rb_kv_node, node);
		memcpy(key->data, wrap->node->key.data, wrap->node->key.size);
		key->size = wrap->node->key.size;
		memcpy(value->data, wrap->node->val.data, value->size);
	}
#ifdef RB_LOCK
	mutex_unlock(&rb_lock);
#endif
	
	return 0;
}

int db_cursor(DB *db, DB_TXN *txnid, DBC **cursorp, uint32_t flags)
{
	struct __lightfs_dbc_wrap *wrap = kmalloc(sizeof(struct __lightfs_dbc_wrap), GFP_KERNEL);
	if (wrap == NULL) {
		return -ENOMEM;
	}

	wrap->node = NULL;
	*cursorp = &wrap->dbc;
	wrap->left = NULL;
	wrap->right = NULL;

	(*cursorp)->dbp = db;
	(*cursorp)->c_getf_set_range = dbc_c_getf_set_range;
	(*cursorp)->c_getf_next = dbc_c_getf_next;
	(*cursorp)->c_get = dbc_c_get;
	(*cursorp)->c_close = dbc_c_close;
	(*cursorp)->c_set_bounds = dbc_c_set_bounds;
	(*cursorp)->c_getf_current = dbc_c_getf_current;

	return 0;
}

int db_env_create(DB_ENV **envp, uint32_t flags)
{
	(*envp)->i = kmalloc(sizeof(struct __lightfs_db_env_internal), GFP_KERNEL);
	if ((*envp)->i == NULL) {
		kfree(*envp);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&((*envp)->i->rbtree_list));
#ifdef RB_LOCK
	mutex_init(&rb_lock);
#endif

	return 0;
}

int db_create(DB **db, DB_ENV *env, uint32_t flags)
{
	*db = kmalloc(sizeof(DB), GFP_KERNEL);
	(*db)->i = kmalloc(sizeof(struct __lightfs_db_internal), GFP_NOIO);
	if ((*db)->i == NULL) {
		kfree(*db);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&(*db)->i->rbtree_list);
	list_add(&(*db)->i->rbtree_list, &env->i->rbtree_list);
	(*db)->i->kv = RB_ROOT;

	return 0;
}

static struct rb_cache_kv_node *find_val_with_key_cache(DB *db, const DBT *key)
{
	struct rb_node *node = db->i->kv.rb_node;

	while (node) {
		struct rb_cache_kv_node *tmp = container_of(node, struct rb_cache_kv_node, node);
		int result;

		result = env_keycmp(key, &tmp->key);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else {
			return tmp;
		}
	}

	return NULL;
}

static int rb_kv_insert_cache(DB *db, struct rb_cache_kv_node *node)
{
	struct rb_node **new = &(db->i->kv.rb_node), *parent = NULL;

	while (*new) {
		struct rb_cache_kv_node *this = container_of(*new, struct rb_cache_kv_node, node);
		int result = env_keycmp(&node->key, &this->key);
		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return -1;
	}

	rb_link_node(&node->node, parent, new);
	rb_insert_color(&node->node, &(db->i->kv));

	return 0;
}



int db_cache_get(DB *db, DB_TXN *txnid, DBT *key, DBT *value, uint32_t flags)
{
	struct rb_cache_kv_node *node;
	//print_key(__func__, key->data, key->size); 

#ifdef RB_LOCK
	down_read(&rb_sem);
#elif RB_SPIN
	spin_lock(&rb_spin);
#endif
	node = find_val_with_key_cache(db, key);
	if (node == NULL) {
#ifdef RB_LOCK
		up_read(&rb_sem);
#elif RB_SPIN
		spin_unlock(&rb_spin);
#endif
		return DB_NOTFOUND;
	}
	if (node->value.data == NULL) {
#ifdef RB_LOCK
		up_read(&rb_sem);
#elif RB_SPIN
		spin_unlock(&rb_spin);
#endif
		return DB_FOUND_FREE;
	}
	memcpy(value->data, node->value.data, value->size);
#ifdef RB_LOCK
	up_read(&rb_sem);
#elif RB_SPIN
	spin_unlock(&rb_spin);
#endif


	return 0;
}

// file/directory deletion
int db_cache_del(DB *db, DB_TXN *txnid, DBT *key, uint32_t flags)
{
	struct rb_cache_kv_node *node;
	//print_key(__func__, key->data, key->size); 
#ifdef RB_LOCK
	down_write(&rb_sem);
#elif RB_SPIN
	spin_lock(&rb_spin);
#endif
	node = find_val_with_key_cache(db, key);
	if (node == NULL) {
#ifdef RB_LOCK
		up_write(&rb_sem);
#elif RB_SPIN
		spin_unlock(&rb_spin);
#endif

		return DB_NOTFOUND;
	}
	rb_erase(&node->node, &db->i->kv);
#ifdef RB_LOCK
	up_write(&rb_sem);
#elif RB_SPIN
	spin_unlock(&rb_spin);
#endif
	kfree(node->key.data);
	kmem_cache_free(rb_meta_cachep, node->value.data);
	kmem_cache_free(rb_kv_cachep, node);


	return 0;
}

// inode eviction
int db_cache_weak_del(DB *db, DB_TXN *txnid, DBT *key, uint32_t flags)
{
	struct rb_cache_kv_node *node;
	void *data;
#ifdef RB_LOCK
	down_write(&rb_sem);
#elif RB_SPIN
	spin_lock(&rb_spin);
#endif
	node = find_val_with_key_cache(db, key);
	if (node == NULL) {
#ifdef RB_LOCK
		up_write(&rb_sem);
#elif RB_SPIN
		spin_unlock(&rb_spin);
#endif

		return DB_NOTFOUND;
	}
	data = node->value.data;
	node->value.data = NULL;
	node->is_updated = 0;
#ifdef RB_LOCK
	up_write(&rb_sem);
#elif RB_SPIN
	spin_unlock(&rb_spin);
#endif
	if (data)
		kmem_cache_free(rb_meta_cachep, data);

	return 0;
}


int db_cache_put(DB *db, DB_TXN *txnid, DBT *key, DBT *value, uint32_t flags)
{
	/* flags are not used in lightfs */
	struct rb_cache_kv_node *node;
	int ret;

	//print_key(__func__, key->data, key->size); 
#ifdef RB_LOCK
	down_write(&rb_sem);
#elif RB_SPIN
	spin_lock(&rb_spin);
#endif
	node = find_val_with_key_cache(db, key);
	if (node != NULL) {
		//BUG_ON(ret != 0);
		if (node->value.data == NULL) {
			ret = _dbt_copy_meta(&node->value, value);
		} else {
			_dbt_no_alloc_copy(&node->value, value);
			node->is_updated++;
		}
#ifdef RB_LOCK
		up_write(&rb_sem);
#elif RB_SPIN
		spin_unlock(&rb_spin);
#endif
		return 0;
	}

	node = kmem_cache_alloc(rb_kv_cachep, GFP_NOIO);
	ret = _dbt_copy(&node->key, key);
	ret = _dbt_copy_meta(&node->value, value);
	ret = rb_kv_insert_cache(db, node);
	node->is_updated = 0;
#ifdef RB_LOCK
	up_write(&rb_sem);
#elif RB_SPIN
	spin_unlock(&rb_spin);
#endif
	return ret;
}



static void free_cache_rb_tree(struct rb_node *node)
{
	struct rb_cache_kv_node *kv_node = container_of(node, struct rb_cache_kv_node, node);
	if (!node)
		return;
	if (node->rb_left)
	  free_cache_rb_tree(node->rb_left);
	if (node->rb_right)
	  free_cache_rb_tree(node->rb_right);
	kfree(kv_node->key.data);
	kfree(kv_node->value.data);
	//kfree(kv_node);
	kmem_cache_free(rb_kv_cachep, kv_node);
}

int db_cache_close(DB *db, uint32_t flag)
{
	free_cache_rb_tree(db->i->kv.rb_node);
	kfree(db->i);
	kmem_cache_destroy(rb_meta_cachep);
	kmem_cache_destroy(rb_kv_cachep);
	return 0;
}

int db_cache_create(DB **db, DB_ENV *env, uint32_t flags)
{
	(*db)->i = kmalloc(sizeof(struct __lightfs_db_internal), GFP_NOIO);
	if ((*db)->i == NULL) {
		kfree(*db);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&(*db)->i->rbtree_list);
	list_add(&(*db)->i->rbtree_list, &env->i->rbtree_list);
	(*db)->i->kv = RB_ROOT;
#ifdef RB_LOCK
	init_rwsem(&rb_sem);
#elif RB_SPIN
	spin_lock_init(&rb_spin);
#endif
	rb_kv_cachep = kmem_cache_create("lightfs_kv_cachep", sizeof(struct rb_cache_kv_node), 0, SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, NULL);
	if (!rb_kv_cachep)
		kmem_cache_destroy(rb_kv_cachep);

	rb_meta_cachep = kmem_cache_create("lightfs_meta_cachep", INODE_SIZE, 0, SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, NULL);
	if (!rb_meta_cachep)
		kmem_cache_destroy(rb_meta_cachep);

	
	return 0;
}


#endif /* RBTREE_KV_H */

#include <linux/kernel.h>
#include <linux/slab.h>

#include "lightfs.h"
#include "lightfs_fs.h"
#include "lightfs_db_env.h"
#include "lightfs_db.h"
#include "lightfs_cache.h"
#include "lightfs_reada.h"
#include "rbtreekv.h"

size_t db_cachesize;

#define DB_ENV_PATH "/db"
#define DATA_DB_NAME "lightfs_data"
#define META_DB_NAME "lightfs_meta"

// XXX: delete these 2 variables once southbound dependency is solved
static DB_ENV *XXX_db_env;
static DB *XXX_data_db;
static DB *XXX_meta_db;
static DB *XXX_cache_db;

#ifdef DATA_CHECK
DB *XXX_checking_db;
#endif

static char ino_key[] = "m\x00\x00\x00\x00\x00\x00\x00\x00next_ino";

static char meta_desc_buf[] = "meta";
static char data_desc_buf[] = "data";
static DBT meta_desc = {
	.data = meta_desc_buf,
	.size = sizeof(meta_desc_buf),
	.ulen = sizeof(meta_desc_buf),
	.flags = DB_DBT_USERMEM,
};
static DBT data_desc = {
	.data = data_desc_buf,
	.size = sizeof(data_desc_buf),
	.ulen = sizeof(data_desc_buf),
	.flags = DB_DBT_USERMEM,
};

extern int
alloc_data_dbt_from_meta_dbt(DBT *data_dbt, DBT *meta_dbt, uint64_t block_num);

extern int
alloc_child_meta_dbt_from_meta_dbt(DBT *dbt, DBT *parent_dbt, const char *name);

extern void
copy_data_dbt_from_inode(DBT *data_dbt, struct inode *inode, uint64_t block_num);

extern int
alloc_data_dbt_from_inode(DBT *data_dbt, struct inode *inode, uint64_t block_num);

extern int
alloc_data_dbt_from_ino(DBT *data_dbt, uint64_t ino, uint64_t block_num);

extern int
alloc_child_meta_dbt_from_inode(DBT *dbt, struct inode *dir, const char *name);

extern void
copy_data_dbt_from_meta_dbt(DBT *data_dbt, DBT *meta_dbt, uint64_t block_num);

extern int
alloc_meta_dbt_prefix(DBT *prefix_dbt, DBT *meta_dbt);

extern void copy_meta_dbt_from_ino(DBT *dbt, uint64_t ino);

static void
copy_child_meta_dbt_from_meta_dbt(DBT *dbt, DBT *parent_dbt, const char *name)
{
	char *parent_key = parent_dbt->data;
	char *meta_key = dbt->data;
	size_t size;
	char *last_slash;

	if ((lightfs_key_path(parent_key))[0] == '\0')
		size = parent_dbt->size + strlen(name) + 2;
	else
		size = parent_dbt->size + strlen(name) + 1;
	BUG_ON(size > dbt->ulen);
	lightfs_key_set_magic(meta_key, META_KEY_MAGIC);
	lightfs_key_copy_ino(meta_key, parent_key);
	if ((lightfs_key_path(parent_key))[0] == '\0') {
		sprintf(lightfs_key_path(meta_key), "\x01\x01%s", name);
	} else {
		last_slash = strrchr(lightfs_key_path(parent_key), '\x01');
		BUG_ON(last_slash == NULL);
		memcpy(lightfs_key_path(meta_key), lightfs_key_path(parent_key),
		       last_slash - lightfs_key_path(parent_key));
		sprintf(lightfs_key_path(meta_key) + (last_slash - lightfs_key_path(parent_key)),
		        "%s\x01\x01%s", last_slash + 1, name);
		//lightfs_error(__func__, "the key path=%s, parent=%s\n", lightfs_key_path(meta_key), lightfs_key_path(parent_key));
	}

	dbt->size = size;
}

static void
copy_child_data_dbt_from_meta_dbt(DBT *dbt, DBT *parent_dbt,
                                  const char *name, uint64_t block_num)
{
	char *parent_key = parent_dbt->data;
	char *data_key = dbt->data;
	size_t size;
	char *last_slash;

	if ((lightfs_key_path(parent_key))[0] == '\0')
		size = parent_dbt->size + strlen(name) + 2;
	else
		size = parent_dbt->size + strlen(name) + 1;
	size += DATA_META_KEY_SIZE_DIFF;
	BUG_ON(size > dbt->ulen);
	lightfs_key_set_magic(data_key, DATA_KEY_MAGIC);
	lightfs_key_copy_ino(data_key, parent_key);
	lightfs_data_key_set_blocknum(data_key, size, block_num);
	if ((lightfs_key_path(parent_key))[0] == '\0') {
		sprintf(lightfs_key_path(data_key), "\x01\x01%s", name);
	} else {
		last_slash = strrchr(lightfs_key_path(parent_key), '\x01');
		BUG_ON(last_slash == NULL);
		memcpy(lightfs_key_path(data_key), lightfs_key_path(parent_key),
		       last_slash - lightfs_key_path(parent_key));
		sprintf(lightfs_key_path(data_key) + (last_slash - lightfs_key_path(parent_key)),
		        "%s\x01\x01%s", last_slash + 1, name);
	}

	dbt->size = size;
}

static void
copy_child_meta_dbt_from_inode(DBT *dbt, struct inode *dir, const char *name)
{
	char *meta_key = dbt->data;
	size_t size;
	uint64_t parent_ino = dir->i_ino;

	size = PATH_POS + strlen(name) + 1;
	BUG_ON(size > dbt->ulen);
	lightfs_key_set_magic(meta_key, META_KEY_MAGIC);
	lightfs_key_set_ino(meta_key, parent_ino);
	sprintf(lightfs_key_path(meta_key), "%s", name);

	dbt->size = size;
}

#if 0
static void
copy_child_data_dbt_from_inode(DBT *data_dbt, struct inode *inode,
                                  const char *name, uint64_t block_num)
{
	char *data_key = data_dbt->data;
	size_t size;
	uint64_t ino = inode->i_ino;

	size = PATH_POS + DATA_META_KEY_SIZE_DIFF;
	BUG_ON(size > data_dbt->ulen);
	lightfs_key_set_magic(data_key, DATA_KEY_MAGIC);
	lightfs_key_set_ino(data_key, ino);
	lightfs_data_key_set_blocknum(data_key, size, block_num);

	data_dbt->size = size;
}
#endif

static inline void
copy_subtree_max_meta_dbt_from_meta_dbt(DBT *dbt, DBT *parent_dbt)
{
	copy_child_meta_dbt_from_meta_dbt(dbt, parent_dbt, "");
	*((char *)(dbt->data + dbt->size - 2)) = '\xff';
	//lightfs_error(__func__, "the key path=%s after\n", lightfs_key_path(dbt->data));
}

static inline void
copy_subtree_max_data_dbt_from_meta_dbt(DBT *dbt, DBT *parent_dbt)
{
	copy_child_data_dbt_from_meta_dbt(dbt, parent_dbt, "", 0);
	*((char *)(dbt->data + dbt->size - sizeof(uint64_t) - 2)) = '\xff';
}

#if 0
static int
meta_key_is_child_of_meta_key(char *child_key, char *parent_key)
{
	//print_key(__func__, child_key, 10);
	//print_key(__func__, parent_key, 10);
	if (lightfs_key_get_ino(child_key) != lightfs_key_get_ino(parent_key))
		return 0;
	else
		return 1;
}
#endif

static int
meta_key_is_child_of_ino(char *child_key, ino_t ino)
{
	if (lightfs_key_get_ino(child_key) != ino)
		return 0;
	else
		return 1;
}

// get the ino_num counting stored in meta_db
// for a brand new DB, it will init ino_num in meta_db (so it may write)
int lightfs_bstore_get_ino(DB *meta_db, DB_TXN *txn, ino_t *ino)
{
	int ret;
	DBT ino_key_dbt, ino_val_dbt;

	dbt_setup(&ino_key_dbt, ino_key, sizeof(ino_key));
	dbt_setup(&ino_val_dbt, ino, sizeof(*ino));

	ret = meta_db->get(meta_db, txn, &ino_key_dbt,
	                   &ino_val_dbt, LIGHTFS_META_GET);
	if (ret == DB_NOTFOUND) {
		*ino = LIGHTFS_ROOT_INO + 1;
		ret = meta_db->put(meta_db, txn, &ino_key_dbt,
		                   &ino_val_dbt, LIGHTFS_META_SET);
	}

	return ret;
}

// get the ino_num counting in meta_db
// if it is smaller than our ino, update that with our ino
int lightfs_bstore_update_ino(DB *meta_db, DB_TXN *txn, ino_t ino)
{
	int ret;
	ino_t curr_ino;
	DBT ino_key_dbt, ino_val_dbt;

	dbt_setup(&ino_key_dbt, ino_key, sizeof(ino_key));
	dbt_setup(&ino_val_dbt, &curr_ino, sizeof(curr_ino));

	ret = meta_db->get(meta_db, txn, &ino_key_dbt,
	                   &ino_val_dbt, LIGHTFS_META_GET);
	if (!ret && ino > curr_ino) {
		curr_ino = ino;
		ret = meta_db->put(meta_db, txn, &ino_key_dbt,
		                   &ino_val_dbt, LIGHTFS_META_SET);
	}

	return ret;
}

static int env_keycmp(DB *DB, DBT const *a, DBT const *b)
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

static struct lightfs_db_key_operations lightfs_key_ops = {
	.keycmp       = env_keycmp,
};

/*
 * block update callback info
 * set value in [offset, offset + size) to buf
 */
struct block_update_cb_info {
	loff_t offset;
	size_t size;
	char buf[];
};

static int
env_update_cb(DB *db, const DBT *key, const DBT *old_val, const DBT *extra,
              void (*set_val)(const DBT *newval, void *set_extra),
              void *set_extra)
{
	DBT val;
	size_t newval_size;
	void *newval;
	const struct block_update_cb_info *info = extra->data;

	if (info->size == 0) {
		// info->size == 0 means truncate
		if (!old_val) {
			newval_size = 0;
			newval = NULL;
		} else {
			newval_size = info->offset;
			if (old_val->size < newval_size) {
				// this means we should keep the old val
				// can we just forget about set_val in this case?
				// idk, to be safe, I did set_val here
				newval_size = old_val->size;
			}
			// now we guaranteed old_val->size >= newval_size
			newval = kmalloc(newval_size, GFP_NOIO);
			if (!newval)
				return -ENOMEM;
			memcpy(newval, old_val->data, newval_size);
		}
	} else {
		// update [info->offset, info->offset + info->size) to info->buf
		newval_size = info->offset + info->size;
		if (old_val && old_val->size > newval_size)
			newval_size = old_val->size;
		newval = kmalloc(newval_size, GFP_NOIO);
		if (!newval)
			return -ENOMEM;
		if (old_val) {
			// copy old val here
			memcpy(newval, old_val->data, old_val->size);
			// fill the place that is not covered by old_val
			//  nor info->buff with 0
			if (info->offset > old_val->size)
				memset(newval + old_val->size, 0,
				       info->offset - old_val->size);
		} else {
			if (info->offset > 0)
				memset(newval, 0, info->offset);
		}
		memcpy(newval + info->offset, info->buf, info->size);
	}

	dbt_setup(&val, newval, newval_size);
	set_val(&val, set_extra);
	kfree(newval);

	return 0;
}

/*
 * Set up DB environment.
 */
int lightfs_bstore_env_open(struct lightfs_sb_info *sbi)
{
	int r;
	uint32_t db_env_flags, db_flags;
	uint32_t giga_bytes, bytes;
	DB_TXN *txn = NULL;
	DB_ENV *db_env;

	BUG_ON(sbi->db_env || sbi->data_db || sbi->meta_db);

	r = lightfs_db_env_create(&sbi->db_env, 0);
	if (r != 0) {
		goto err;
	}

	db_env = sbi->db_env;

	giga_bytes = db_cachesize / (1L << 30);
	bytes = db_cachesize % (1L << 30);
	r = db_env->set_cachesize(db_env, giga_bytes, bytes, 1);
	if (r != 0)
		goto err;
	r = db_env->set_key_ops(db_env, &lightfs_key_ops);
	if (r != 0)
		goto err;

	db_env->set_update(db_env, env_update_cb);

	db_env_flags = DB_CREATE | DB_PRIVATE | DB_THREAD | DB_INIT_MPOOL |
	               DB_INIT_LOCK | DB_RECOVER | DB_INIT_LOG | DB_INIT_TXN;

	r = db_env->open(db_env, DB_ENV_PATH, db_env_flags, 0755);
	if (r) {
		r = -ENOENT;
		goto err;
	}

	db_flags = DB_CREATE | DB_THREAD;
	r = lightfs_db_create(&sbi->data_db, db_env, 0);
	if (r)
		goto err_close_env;
	r = lightfs_db_create(&sbi->meta_db, db_env, 0);
	if (r)
		goto err_close_env;

	r = lightfs_cache_create(&sbi->cache_db, db_env, 0);
	if (r)
		goto err_close_env;

	r = lightfs_bstore_txn_begin(db_env, NULL, &txn, TXN_READONLY);
	if (r)
		goto err_close_env;
	r = sbi->data_db->open(sbi->data_db, txn, DATA_DB_NAME, NULL,
	                       DB_BTREE, db_flags, 0644);
	if (r) {
		lightfs_bstore_txn_abort(txn);
		goto err_close_env;
	}
	r = sbi->data_db->change_descriptor(sbi->data_db, txn, &data_desc, DB_UPDATE_CMP_DESCRIPTOR);
	if (r) {
		lightfs_bstore_txn_abort(txn);
		goto err_close_env;
	}
	r = sbi->meta_db->open(sbi->meta_db, txn, META_DB_NAME, NULL,
	                       DB_BTREE, db_flags, 0644);
	if (r) {
		lightfs_bstore_txn_abort(txn);
		goto err_close_env;
	}
	r = sbi->meta_db->change_descriptor(sbi->meta_db, txn, &meta_desc, DB_UPDATE_CMP_DESCRIPTOR);
	if (r) {
		lightfs_bstore_txn_abort(txn);
		goto err_close_env;
	}

	r = lightfs_bstore_txn_commit(txn, DB_TXN_SYNC);
	if (r)
		goto err_close_env;

	XXX_db_env = sbi->db_env;
	XXX_data_db = sbi->data_db;
	XXX_meta_db = sbi->meta_db;
	XXX_cache_db = sbi->cache_db;

#ifdef DATA_CHECK
	db_create(&XXX_checking_db, db_env, 0);
#endif

	return 0;

	sbi->data_db->close(sbi->data_db, 0);
	sbi->meta_db->close(sbi->meta_db, 0);
	sbi->cache_db->close(sbi->cache_db, 0);
err_close_env:
	db_env->close(db_env, 0);
err:
	return r;
}

/*
 * Close DB environment
 */
int lightfs_bstore_env_close(struct lightfs_sb_info *sbi)
{
	int ret = 0;

	if (ret)
		goto out;
	BUG_ON(sbi->data_db == NULL || sbi->meta_db == NULL || sbi->db_env == NULL || sbi->cache_db == NULL);

	ret = sbi->cache_db->close(sbi->cache_db, 0);
	BUG_ON(ret);
	sbi->cache_db = NULL;

	ret = sbi->data_db->close(sbi->data_db, 0);
	BUG_ON(ret);
	sbi->data_db = NULL;

	ret = sbi->meta_db->close(sbi->meta_db, 0);
	BUG_ON(ret);
	sbi->meta_db = NULL;

	ret = sbi->db_env->close(sbi->db_env, 0);
	BUG_ON(ret != 0);
	sbi->db_env = 0;

#ifdef DATA_CHECK
	db_close(XXX_checking_db, 0);
	XXX_checking_db = NULL;
#endif

	XXX_db_env = NULL;
	XXX_data_db = NULL;
	XXX_meta_db = NULL;

out:
	return 0;
}

int lightfs_bstore_meta_get_tmp(DB *meta_db, DBT *meta_dbt, DB_TXN *txn,
                         struct lightfs_metadata *metadata)
{
	int ret = 0;
	DBT value;

	dbt_setup(&value, metadata, sizeof(*metadata));

	//print_key(__func__, meta_dbt->data, meta_dbt->size);
	ret = XXX_cache_db->cache_get(XXX_cache_db, NULL, meta_dbt, &value, 0);

	if (ret == DB_NOTFOUND) {
		ret = -ENOENT;
	} else if (ret == DB_FOUND_FREE) {
		ret = 0;
	}

	return ret;
}

int lightfs_bstore_meta_get(DB *meta_db, DBT *meta_dbt, DB_TXN *txn,
                         struct lightfs_metadata *metadata)
{
	int ret;
	DBT value, tmp;

	dbt_setup(&value, metadata, sizeof(*metadata));

	//print_key(__func__, meta_dbt->data, meta_dbt->size);
	ret = XXX_cache_db->cache_get(XXX_cache_db, NULL, meta_dbt, &value, 0);

	if (ret == DB_NOTFOUND) {
		ret = -ENOENT;
	} else if (ret == DB_FOUND_FREE) {
			dbt_setup(&tmp, metadata, sizeof(*metadata));
			ret = meta_db->get(meta_db, txn, meta_dbt, &tmp, LIGHTFS_META_GET);
	}

	return ret;
}

int lightfs_bstore_group_eviction(struct inode *inode) {
	DBT *dir_meta_dbt;
	dir_meta_dbt = &(LIGHTFS_I(inode)->meta_dbt);
	lightfs_ht_cache_group_eviction(dir_meta_dbt);
	return 0;
}

int lightfs_bstore_meta_put_tmp(DB *meta_db, DBT *meta_dbt, DB_TXN *txn,
                         struct lightfs_metadata *metadata, struct inode *dir_inode, bool is_dir)
{
	DBT value;

	dbt_setup(&value, metadata, sizeof(*metadata));

	return XXX_meta_db->put(meta_db, txn, meta_dbt, &value, LIGHTFS_META_SET);
}

int lightfs_bstore_meta_put_cache(DB *meta_db, DBT *meta_dbt, DB_TXN *txn,
                         struct lightfs_metadata *metadata, struct inode *dir_inode, bool is_dir)
{
	DBT value;

	dbt_setup(&value, metadata, sizeof(*metadata));

	return XXX_cache_db->cache_put(XXX_cache_db, NULL, meta_dbt, &value, 0, dir_inode, is_dir);
}

int lightfs_bstore_meta_put(DB *meta_db, DBT *meta_dbt, DB_TXN *txn,
                         struct lightfs_metadata *metadata, struct inode *dir_inode, bool is_dir)
{
	DBT value;

	dbt_setup(&value, metadata, sizeof(*metadata));

	XXX_cache_db->cache_put(XXX_cache_db, NULL, meta_dbt, &value, 0, dir_inode, is_dir);

	return meta_db->put(meta_db, txn, meta_dbt, &value, LIGHTFS_META_SET);
}

int lightfs_bstore_meta_sync_put(DB *meta_db, DBT *meta_dbt, DB_TXN *txn,
						 struct lightfs_metadata *metadata, struct inode *dir_inode, bool is_dir)
{
	DBT value;

	dbt_setup(&value, metadata, sizeof(*metadata));

	XXX_cache_db->cache_put(XXX_cache_db, NULL, meta_dbt, &value, 0, dir_inode, is_dir);

	//return meta_db->sync_put(meta_db, txn, meta_dbt, &value, LIGHTFS_META_SYNC_SET);
	return meta_db->sync_put(meta_db, txn, meta_dbt, &value, LIGHTFS_META_SET);
}


int lightfs_bstore_meta_del(DB *meta_db, DBT *meta_dbt, DB_TXN *txn, bool is_weak_del, bool is_dir)
{
	if (is_weak_del) {
		XXX_cache_db->cache_weak_del(XXX_cache_db, NULL, meta_dbt, 0);
		return 0;
	} else {
		XXX_cache_db->cache_del(XXX_cache_db, NULL, meta_dbt, 0, is_dir);
	}
	return meta_db->del(meta_db, txn, meta_dbt, LIGHTFS_META_DEL);
}

static unsigned char filetype_table[] = {
	DT_UNKNOWN, DT_FIFO, DT_CHR, DT_UNKNOWN,
	DT_DIR, DT_UNKNOWN, DT_BLK, DT_UNKNOWN,
	DT_REG, DT_UNKNOWN, DT_LNK, DT_UNKNOWN,
	DT_SOCK, DT_UNKNOWN, DT_WHT, DT_UNKNOWN
};

#define lightfs_get_type(mode) filetype_table[(mode >> 12) & 15]

int lightfs_bstore_meta_readdir(DB *meta_db, DBT *meta_dbt, DB_TXN *txn,
                             struct dir_context *ctx, struct inode *inode, struct readdir_ctx *dir_ctx)
{
	int ret = 0, r;
	char *child_meta_key;
	struct lightfs_metadata meta;
	DBT child_meta_dbt, metadata_dbt;
	DBC *cursor;
	char *name;
	u64 ino;
	unsigned type;
	char indirect_meta_key[SIZEOF_ROOT_META_KEY];
	DBT indirect_meta_dbt;
	volatile bool is_next = false;
	static volatile bool debug = false;
#ifdef DISABLE_DCACHE
	DBC *dcursor;
	DBT dchild_meta_dbt, dmetadata_dbt;
	char *dchild_meta_key;
#endif
	loff_t old_pos = ctx->pos;

	//lightfs_error(__func__, "ctx->pos = %p\n", ctx->pos);

	if (ctx->pos == 2) { // iterator start only
		child_meta_key = kmalloc(META_KEY_MAX_LEN, GFP_NOIO);
		if (child_meta_key == NULL)
			return -ENOMEM;
		dbt_setup_buf(&child_meta_dbt, child_meta_key, META_KEY_MAX_LEN);
		copy_child_meta_dbt_from_inode(&child_meta_dbt, inode, "\x01");
#ifdef DISABLE_DCACHE
		dchild_meta_key = kmalloc(META_KEY_MAX_LEN, GFP_NOIO);
		if (dchild_meta_key == NULL)
			return -ENOMEM;
		dbt_setup_buf(&dchild_meta_dbt, dchild_meta_key, META_KEY_MAX_LEN);
#endif
		//if (dir_ctx->pos == 1) // partial hit
		//	is_next = true;
		dir_ctx->pos = (loff_t)(child_meta_key);
		ctx->pos = (loff_t)(dir_ctx);
	} else {
		child_meta_key = (char *)dir_ctx->pos;
		dbt_setup(&child_meta_dbt, child_meta_key, META_KEY_MAX_LEN);
		child_meta_dbt.size = SIZEOF_META_KEY(child_meta_key);
	}

	dbt_setup(&metadata_dbt, &meta, sizeof(meta));
	dbt_setup_buf(&indirect_meta_dbt, indirect_meta_key,
	              SIZEOF_ROOT_META_KEY);
	cursor = dir_ctx->cursor;
	txn = dir_ctx->txn;
		
	//lightfs_error(__func__, "bstore!!!! dir_ctx: %px, dir_ctx->pos: %d, dir->cursor: %px\n", dir_ctx, dir_ctx->pos, dir_ctx->cursor);

	if (is_next) { // parital cache hit
		r = cursor->c_get(cursor, &child_meta_dbt, &metadata_dbt, DB_NEXT);
	} else {
		if (old_pos == 2) { // dcache start only
			memcpy(child_meta_dbt.data, meta_dbt->data, meta_dbt->size);
			child_meta_dbt.size = meta_dbt->size;
#ifdef DISABLE_DCACHE
			copy_child_meta_dbt_from_inode(&dchild_meta_dbt, inode, "\x01");
			dchild_meta_dbt.size = 10;
			dbt_setup(&metadata_dbt, &meta, sizeof(meta));
			metadata_dbt.size = i_size_read(inode);
			dcursor = dir_ctx->dcursor;
			if (!lightfs_inode->is_lookuped) {
				r = dcursor->c_get(dcursor, &dchild_meta_dbt, &metadata_dbt, DB_SET_RANGE);
			} else {
				lightfs_inode->is_lookuped = 1;
			}
			kfree(dchild_meta_key);
#endif
		}
		r = cursor->c_get(cursor, &child_meta_dbt, &metadata_dbt, DB_SET_RANGE);
	}
	while (!r) {
		//print_key(__func__, child_meta_key, child_meta_dbt.size);
		if (!meta_key_is_child_of_ino(child_meta_key, inode->i_ino)) {
			kfree(child_meta_key);
			dir_ctx->pos = 3;
			break;
		}
		if (meta.type == LIGHTFS_METADATA_TYPE_REDIRECT) {
			copy_meta_dbt_from_ino(&indirect_meta_dbt, meta.u.ino);
			r = lightfs_bstore_meta_get(meta_db, &indirect_meta_dbt,
			                         txn, &meta);
			if (r)
				break;
		}
		ino = meta.u.st.st_ino;
		type = lightfs_get_type(meta.u.st.st_mode);
		name = lightfs_key_path(child_meta_key);
		if (debug) {
			debug = false;
		}	
		if (!(ret = dir_emit(ctx, name, strlen(name), ino, type))) {
			break;
		}
		dir_ctx->emit_cnt++;

		r = cursor->c_get(cursor, &child_meta_dbt, &metadata_dbt,
		                  DB_NEXT);
	}

	if (r == DB_NOTFOUND) {
		kfree(child_meta_key);
		dir_ctx->pos = 3;
		r = 0;
	} else if (r == DB_NOTFOUND_DCACHE_FULL) { // all cache hit
		kfree(child_meta_key);
		dir_ctx->pos = 0;
		r = 0;
	} else if (r == DB_NOTFOUND_DCACHE) {
		kfree(child_meta_key);
		if (dir_ctx->emit_cnt) // partial miss
			dir_ctx->pos = 1;
		else // all miss
			dir_ctx->pos = 2;
		r = 0;
	}

	if (r)
		ret = r;

	return ret;
}

int lightfs_bstore_get(DB *data_db, DBT *data_dbt, DB_TXN *txn, void *buf, struct inode *inode)
{
	int ret;
	DBT value;
	loff_t size = i_size_read(inode);
	char *data_key = data_dbt->data;
	size_t block_off = block_get_off_by_position(size);
	uint64_t block_num = lightfs_get_block_num_by_size(size);
	
	dbt_setup(&value, buf, LIGHTFS_BSTORE_BLOCKSIZE);

	ret = data_db->get(data_db, txn, data_dbt, &value, LIGHTFS_DATA_GET);
	if (!ret && (lightfs_data_key_get_blocknum(data_key, data_dbt->size) == block_num) && block_off && (block_off < LIGHTFS_BSTORE_BLOCKSIZE)) {
		memset(buf + block_off, 0, LIGHTFS_BSTORE_BLOCKSIZE - block_off);
	}
	if (ret == DB_NOTFOUND) {
		ret = -ENOENT;
	}

	return ret;
}

// size of buf must be LIGHTFS_BLOCK_SIZE
int lightfs_bstore_put(DB *data_db, DBT *data_dbt, DB_TXN *txn,
                    const void *buf, size_t len, int is_seq)
{
	int ret;
	DBT value;

	dbt_setup(&value, buf, len);

	ret = is_seq ?
	      data_db->seq_put(data_db, txn, data_dbt, &value, LIGHTFS_DATA_SEQ_SET) :
	      data_db->put(data_db, txn, data_dbt, &value, LIGHTFS_DATA_SET);

	return ret;
}

int lightfs_bstore_put_page(DB *data_db, DBT *data_dbt, DB_TXN *txn,
                    struct page *page, size_t len, int is_seq)
{
	int ret;
	DBT value;
#ifdef DATA_CHECK
	char *buf;
	DBT checking_value;
	buf = kmap(page);
	dbt_setup(&checking_value, buf, PAGE_SIZE);
	db_put(XXX_checking_db, NULL, data_dbt, &checking_value, 0);
	kunmap(page);
#endif

	dbt_setup(&value, page, len);

	ret = data_db->put(data_db, txn, data_dbt, &value, LIGHTFS_DATA_SET_WB);

	return ret;
}


int lightfs_bstore_update(DB *data_db, DBT *data_dbt, DB_TXN *txn,
                       const void *buf, size_t size, loff_t offset)
{
	int ret;
	DBT value;

	dbt_setup(&value, buf, size);

	ret = data_db->update(data_db, txn, data_dbt, &value, offset, LIGHTFS_DATA_UPDATE);

	return ret;
}

// delete all blocks that is beyond new_num
//  if offset == 0, delete block new_num as well
//  otherwise, truncate block new_num to size offset
int lightfs_bstore_trunc(DB *data_db, DBT *meta_dbt,
                      DB_TXN *_txn, uint64_t new_num, uint64_t offset, struct inode *inode)
{
	int ret;
	DBT min_data_key_dbt, max_data_key_dbt;
	loff_t size = i_size_read(inode);
	uint64_t last_block_num = lightfs_get_block_num_by_size(size);
	uint64_t current_block_num, total_block_num, transfering_block_cnt;
	DB_TXN *txn = _txn;
	if (new_num == 0) {
		current_block_num = 1;
	} else {
		current_block_num = (offset == 0) ? new_num : (new_num + 1);
	}

	ret = alloc_data_dbt_from_inode(&min_data_key_dbt, inode,
		current_block_num);
	if (ret)
		return ret;

	ret = alloc_data_dbt_from_inode(&max_data_key_dbt, inode, current_block_num);
	if (ret) {
		dbt_destroy(&min_data_key_dbt);
		return ret;
	}

	//print_key(__func__, min_data_key_dbt.data, min_data_key_dbt.size);
	//pr_info("last block num: %d, current block_num: %d\n", last_block_num, current_block_num);
	total_block_num = last_block_num - current_block_num + 1;

#ifdef PINK
	while (total_block_num) {
		TXN_GOTO_LABEL(retry);
		lightfs_bstore_txn_begin(sbi->db_dev, NULL, &txn, TXN_MAY_WRITE);
		transfering_block_cnt = total_block_num > LIGHTFS_TXN_LIMIT ? LIGHTFS_TXN_LIMIT : total_block_num;
		ret = data_db->del_multi(data_db, txn, &min_data_key_dbt, transfering_block_cnt, 0, LIGHTFS_DATA_DEL_MULTI);
		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
		} else {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			COMMIT_JUMP_ON_CONFLICT(ret, retry);
		}
		current_block_num += transfering_block_cnt;
		total_block_num -= transfering_block_cnt;
		copy_data_dbt_from_inode(&min_data_key_dbt, inode, current_block_num);
	}
#else
	do {
		ret = data_db->del(data_db, txn, &max_data_key_dbt, LIGHTFS_DATA_DEL);
		if (ret) {
			lightfs_error(__func__, "Shouldn't reach\n");
		} else {
			current_block_num++;
			copy_data_dbt_from_inode(&max_data_key_dbt, inode, current_block_num);
		}

	} while (last_block_num >= current_block_num);
#endif

	/*
	if (!ret && offset) {
		TXN_GOTO_LABEL(update_retry);
		lightfs_bstore_txn_begin(sbi->db_dev, NULL, &txn, TXN_MAY_WRITE);
		dbt_setup(&value, NULL, 0);
		lightfs_data_key_set_blocknum(((char *)min_data_key_dbt.data),
		                           min_data_key_dbt.size, new_num);
		ret = data_db->update(data_db, txn, &min_data_key_dbt,
		                      &value, offset, LIGHTFS_DATA_UPDATE);
		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, update_retry);
			lightfs_bstore_txn_abort(txn);
		} else {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			COMMIT_JUMP_ON_CONFLICT(ret, update_retry);
		}
	}
	*/

	dbt_destroy(&max_data_key_dbt);
	dbt_destroy(&min_data_key_dbt);

	return ret;
}

int lightfs_bstore_scan_one_page(DB *data_db, DBT *meta_dbt, DB_TXN *txn, struct page *page, struct inode *inode)
{
	int ret;
	DBT data_dbt;
	char *buf;

#ifdef DATA_CHECK
	int chk_ret;
	char *chk_buf;
	DBT chk_value;
#endif

	//// now data_db keys start from 1
	uint64_t page_block_num = PAGE_TO_BLOCK_NUM(page);
	loff_t size = i_size_read(inode);
	uint64_t last_block_num = lightfs_get_block_num_by_size(size);

	if (page_block_num > last_block_num) {
		lightfs_error(__func__, "block num: %d, size: %d\n", page_block_num, size);
		zero_user_segment(page, 0, PAGE_SIZE);
		//buf = kmap_atomic(page);
		//memset(buf, 0, LIGHTFS_BSTORE_BLOCKSIZE);
		//kunmap_atomic(buf);
		return 0;
	}
	ret = alloc_data_dbt_from_inode(&data_dbt, inode, page_block_num);
	if (ret)
		return ret;

	buf = kmap_atomic(page);
	ret = lightfs_bstore_get(data_db, &data_dbt, txn, buf, inode);
#ifdef DATA_CHECK
	chk_buf = kmalloc(LIGHTFS_BSTORE_BLOCKSIZE, GFP_KERNEL);
	dbt_setup(&chk_value, chk_buf, LIGHTFS_BSTORE_BLOCKSIZE);
	chk_ret = db_get(XXX_checking_db, NULL, &data_dbt, &chk_value, 0);
	if (ret == -ENOENT) {
		if (chk_ret != DB_NOTFOUND) {
			kfree(chk_buf);
			BUG_ON(1);
		} 
	} else {
		if (chk_ret == DB_NOTFOUND) {
			kfree(chk_buf);
			BUG_ON(1);
		} else {
			if (memcmp(buf, chk_buf, LIGHTFS_BSTORE_BLOCKSIZE)) {
				pr_info("ERROR\n");
				memcpy(buf, chk_buf, LIGHTFS_BSTORE_BLOCKSIZE);
				kfree(chk_buf);
				//BUG_ON(1);
			} else {
				kfree(chk_buf);
			}
		}
		
	}
#endif
	if (ret == -ENOENT) {
		memset(buf, 0, LIGHTFS_BSTORE_BLOCKSIZE);
		ret = 0;
	}
	kunmap_atomic(buf);
	flush_dcache_page(page);

	dbt_destroy(&data_dbt);

	return ret;
}

struct lightfs_scan_pages_cb_info {
	char *meta_key;
	struct lightfs_io *lightfs_io;
	int do_continue;
	struct inode *inode;
	uint64_t block_cnt;
};

static int lightfs_scan_pages_cb(DBT const *key, DBT const *val, void *extra)
{
	char *data_key = key->data;
	struct lightfs_scan_pages_cb_info *info = extra;
	struct lightfs_io *lightfs_io = info->lightfs_io;
	size_t block_off;
	uint64_t block_num;
	loff_t size;
#ifdef DATA_CHECK
	char *chk_buf;
	DBT chk_value;
	int chk_ret;
#endif

	if (key_is_same_of_ino(data_key, info->inode->i_ino)) {
		struct page *page = lightfs_io_current_page(lightfs_io);
		uint64_t page_block_num = PAGE_TO_BLOCK_NUM(page);
		char *page_buf;

		//print_key(__func__, key->data, key->size);
		//lightfs_error(__func__, "page_block_num: %d, info->block_cnt: %d\n", page_block_num, info->block_cnt);

		while (page_block_num < lightfs_data_key_get_blocknum(data_key, key->size)) {
			zero_user_segment(page, 0, PAGE_SIZE);
			/*
			page_buf = kmap_atomic(page);
			memset(page_buf, 0, PAGE_SIZE);
			kunmap_atomic(page_buf);
			*/

			lightfs_io_advance_page(lightfs_io);
			if (lightfs_io_job_done(lightfs_io))
				break;
			page = lightfs_io_current_page(lightfs_io);
			page_block_num = PAGE_TO_BLOCK_NUM(page);
			info->block_cnt--;
		}

		if (page_block_num == lightfs_data_key_get_blocknum(data_key, key->size)) {
			size = i_size_read(info->inode);
			block_off = block_get_off_by_position(size);
			block_num = lightfs_get_block_num_by_size(size);
			page_buf = kmap_atomic(page);
			if (!page_buf || !val->data) {
				BUG_ON(1);
			}
			if (val->size)
				memcpy(page_buf, val->data, val->size);
			kunmap_atomic(page_buf);
#ifdef DATA_CHECK
			chk_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
			dbt_setup(&chk_value, chk_buf, PAGE_SIZE);
			chk_ret = db_get(XXX_checking_db, NULL, key, &chk_value, 0);
			if (chk_ret == DB_NOTFOUND) {
				if (*page_buf != 0) {
					kfree(chk_buf);
					BUG_ON(1);
				} else {
					kfree(chk_buf);
				}
			} else {
				if (memcmp(page_buf, chk_buf, val->size)) {
					memcmp(page_buf, chk_buf, val->size);
					kfree(chk_buf);
				} else {
					kfree(chk_buf);
				}
			}
#endif
			if (page_block_num == block_num && block_off && block_off < PAGE_SIZE) {
				//pr_info("last block!!! block_num: %d, block_off: %d\n", block_num, block_off);
				zero_user_segment(page, block_off, PAGE_SIZE);
			}
			//kunmap_atomic(page_buf);
			flush_dcache_page(page);
			lightfs_io_advance_page(lightfs_io);
			info->block_cnt--;
		}

		info->do_continue = !lightfs_io_job_done(lightfs_io);
	} else
		info->do_continue = 0;

	return 0;
}

static inline void lightfs_bstore_fill_rest_page(struct lightfs_io *lightfs_io)
{
	struct page *page;

	while (!lightfs_io_job_done(lightfs_io)) {
		page = lightfs_io_current_page(lightfs_io);
		zero_user_segment(page, 0, PAGE_SIZE);
		//page_buf = kmap_atomic(page);
		//memset(page_buf, 0, PAGE_SIZE);
		//kunmap_atomic(page_buf);
		lightfs_io_advance_page(lightfs_io);
	}
}

int lightfs_bstore_scan_pages(DB *data_db, DBT *meta_dbt, DB_TXN *txn, struct lightfs_io *lightfs_io, struct inode *inode)
{
	int ret, r = 0;
	struct lightfs_scan_pages_cb_info info;
	DBT data_dbt;
	loff_t size = i_size_read(inode);
	uint64_t current_block_num, block_cnt, last_block_num;
	struct page *page;

	if (lightfs_io_job_done(lightfs_io))
		return 0;

	page = lightfs_io_current_page(lightfs_io);
	current_block_num = PAGE_TO_BLOCK_NUM(page);
	last_block_num = PAGE_TO_BLOCK_NUM(lightfs_io_last_page(lightfs_io));
	block_cnt = lightfs_get_block_num_by_size(size) - current_block_num + 1;
	block_cnt = block_cnt > last_block_num - current_block_num + 1 ? last_block_num - current_block_num + 1: block_cnt; 
	
	if (block_cnt == 0) {
		WARN_ON(1);
		lightfs_bstore_fill_rest_page(lightfs_io);
	}
	
	ret = alloc_data_dbt_from_inode(&data_dbt, inode,
			current_block_num);
	if (ret)
		return ret;

#ifdef GET_MULTI
	info.meta_key = meta_dbt->data;
	info.lightfs_io = lightfs_io;
	info.inode = inode;
	info.block_cnt = block_cnt;

	while (info.do_continue && !r)
		r = data_db->get_multi(data_db, txn, &data_dbt, block_cnt, lightfs_scan_pages_cb, &info, LIGHTFS_GET_MULTI);
	if (r && r != DB_NOTFOUND)
		ret = r;
	if (!ret)
		lightfs_bstore_fill_rest_page(lightfs_io);

	BUG_ON(r);
#else
	if (block_cnt < 100) {
		while(block_cnt--) {
			buf = kmap_atomic(page);
			ret = lightfs_bstore_get(data_db, &data_dbt, txn, buf, inode);
			if (ret == -ENOENT) {
				memset(buf, 0, LIGHTFS_BSTORE_BLOCKSIZE);
				ret = 0;
			}
			kunmap_atomic(buf);
			if (!(lightfs_io_current_page(lightfs_io) == lightfs_io_last_page(lightfs_io))) {
				lightfs_io_advance_page(lightfs_io);
				lightfs_bstore_fill_rest_page(lightfs_io);
			} else {
				lightfs_io_advance_page(lightfs_io);
			}
		}
	} else {
		ret = data_db->cursor(data_db, txn, &cursor, LIGHTFS_DATA_CURSOR);
		if (ret)
			goto free_out;

		info.meta_key = meta_dbt->data;
		info.lightfs_io = lightfs_io;
		info.inode = inode;
		info.block_cnt = block_cnt;

		r = cursor->c_getf_set_range(cursor, info.block_cnt, &data_dbt, lightfs_scan_pages_cb, &info);
		while (info.do_continue && !r)
			r = cursor->c_getf_next(cursor, info.block_cnt, lightfs_scan_pages_cb, &info);
		if (r && r != DB_NOTFOUND)
			ret = r;
		if (!ret)
			lightfs_bstore_fill_rest_page(lightfs_io);
	
		r = cursor->c_close(cursor);
		BUG_ON(r);
	}
#endif
	dbt_destroy(&data_dbt);

	return ret;
}

#ifdef READA
int lightfs_bstore_reada_pages(DB *data_db, DBT *meta_dbt, DB_TXN *txn, struct lightfs_io *lightfs_io, struct inode *inode, unsigned iter)
{
	int ret, r = 0;
	DBT data_dbt;
	loff_t size = i_size_read(inode);
	uint64_t current_block_num, block_cnt;
	struct reada_entry *last_ra_entry, *ra_entry;
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
	struct page *page;

	if (lightfs_inode->ra_entry) {
		last_ra_entry = list_last_entry(&lightfs_inode->ra_list, struct reada_entry, list);
		current_block_num = last_ra_entry->reada_block_start + last_ra_entry->reada_block_len;
	} else {
		page = lightfs_io_last_page(lightfs_io);
		current_block_num = (PAGE_TO_BLOCK_NUM(page) * iter) + 1;
	}
	block_cnt = lightfs_get_block_num_by_size(size) + 1 - current_block_num;
	block_cnt = block_cnt > lightfs_io->lightfs_vcnt * READA_MULTIPLIER ? lightfs_io->lightfs_vcnt * READA_MULTIPLIER : block_cnt; 

	if (block_cnt == 0) {
		return EEOF;
	}
	
	ret = alloc_data_dbt_from_inode(&data_dbt, inode,
			current_block_num);
	if (ret)
		return ret;

	ra_entry = lightfs_reada_alloc(inode, current_block_num, block_cnt);

	r = data_db->get_multi_reada(data_db, txn, &data_dbt, block_cnt, ra_entry, LIGHTFS_GET_MULTI_READA);

	dbt_destroy(&data_dbt);

	return ret;
}
#endif

struct lightfs_die_cb_info {
	char *meta_key;
	int *is_empty;
	ino_t ino;
	uint64_t block_cnt;
};

static int lightfs_die_cb(DBT const *key, DBT const *val, void *extra)
{
	struct lightfs_die_cb_info *info = extra;
	char *current_meta_key = key->data;

	*(info->is_empty) = !meta_key_is_child_of_ino(current_meta_key, info->ino);

	return 0;
}

int lightfs_dir_is_empty(DB *meta_db, DBT *meta_dbt, DB_TXN *txn, int *is_empty, struct inode *inode)
{
	int ret, r;
	struct lightfs_die_cb_info info;
	DBT start_meta_dbt;
	DBC *cursor;

	return 1;

	ret = alloc_child_meta_dbt_from_inode(&start_meta_dbt, inode, "");
	if (ret)
		return ret;

	ret = meta_db->cursor(meta_db, txn, &cursor, LIGHTFS_META_CURSOR);
	if (ret)
		goto out;

	info.meta_key = meta_dbt->data;
	info.is_empty = is_empty;
	info.ino = inode->i_ino;
	info.block_cnt = 1;
	ret = cursor->c_getf_set_range(cursor, info.block_cnt, &start_meta_dbt, lightfs_die_cb, &info);
	if (ret == DB_NOTFOUND) {
		ret = 0;
		*is_empty = 1;
	}

	r = cursor->c_close(cursor);
	BUG_ON(r);
out:
	dbt_destroy(&start_meta_dbt);

	return ret;
}

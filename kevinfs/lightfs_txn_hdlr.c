#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/completion.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include "lightfs_txn_hdlr.h"
#include "lightfs_io.h"
#include "rbtreekv.h"
#include "lightfs_queue.h"

static struct kmem_cache *lightfs_c_txn_cachep;
static struct kmem_cache *lightfs_txn_cachep;
static struct kmem_cache *lightfs_txn_buf_cachep;
static struct kmem_cache *lightfs_buf_cachep;
static struct kmem_cache *lightfs_dbc_cachep;
static struct kmem_cache *lightfs_dbc_buf_cachep;
static struct kmem_cache *lightfs_meta_buf_cachep;

static struct __lightfs_txn_hdlr *txn_hdlr;

#ifdef TXN_BUFFER
static int lightfs_txn_buffer_get(struct rb_root *root, DB_TXN_BUF *txn_buf);
static DB_TXN_BUF *lightfs_txn_buffer_put(struct rb_root *root, DB_TXN_BUF *txn_buf);
static int lightfs_txn_buffer_del(struct rb_root *root, DB_TXN_BUF *txn_buf);
#endif

static void lightfs_c_txn_transfer_work(struct work_struct *work);
static void lightfs_c_txn_commit_flush_work(struct work_struct *work);
static void lightfs_c_txn_commit_work(struct work_struct *work);

static inline void lightfs_c_txn_init(void *c_txn)
{
	DB_C_TXN *_c_txn = c_txn;
	memset(c_txn, 0, sizeof(DB_C_TXN));
	INIT_LIST_HEAD(&_c_txn->c_txn_list);
	INIT_LIST_HEAD(&_c_txn->txn_list);
	INIT_LIST_HEAD(&_c_txn->children);
	_c_txn->size = 0;
	_c_txn->cnt = 0;
	_c_txn->filter = (struct bloomfilter *)kmalloc(sizeof(struct bloomfilter) + C_TXN_BLOOM_M_BYTES, GFP_NOIO);
	_c_txn->state = TXN_CREATED;
	_c_txn->parents = 0;
	bloomfilter_init(_c_txn->filter, C_TXN_BLOOM_M_BYTES * 8, C_TXN_BLOOM_K);
	INIT_WORK(&_c_txn->transfer_work, lightfs_c_txn_transfer_work);
	INIT_WORK(&_c_txn->commit_work, lightfs_c_txn_commit_flush_work);
	INIT_WORK(&_c_txn->work, lightfs_c_txn_commit_work);
	_c_txn->committing_cnt = 0;
}

static inline void lightfs_c_txn_free(DB_C_TXN *c_txn)
{
	kfree(c_txn->filter);
	kmem_cache_free(lightfs_c_txn_cachep, c_txn);
}

static inline void lightfs_txn_init(void *txn)
{
	DB_TXN *_txn= txn;
	memset(txn, 0, sizeof(DB_TXN));
	INIT_LIST_HEAD(&_txn->txn_list);
	INIT_LIST_HEAD(&_txn->txn_buf_list);
	_txn->cnt = 0;
	_txn->size = sizeof(_txn->cnt) + sizeof(_txn->size); // txn->cnt, txn->size
	_txn->state = TXN_CREATED;
#ifdef TXN_TIME_CHECK
	_txn->is_inserted = 0;
	lightfs_get_time(&_txn->create);
#endif
	_txn->completionp = NULL;
}

static inline void lightfs_txn_free(DB_TXN *txn) {
#ifdef TXN_TIME_CHECK
	lightfs_get_time(&txn->free);
	if (txn->state != TXN_READ) {
		pr_info("[COMMIT:%d]\n begin ~ wakeup: %d\n wakeup ~ create: %d\n create ~ 1'st insert: %d\n 1'st insert ~ last insert: %d\n last insert ~ commit: %d\n commit ~ free %d\n", txn->txn_id, lightfs_time_check(txn->begin, txn->wakeup), lightfs_time_check(txn->wakeup, txn->create), lightfs_time_check(txn->create, txn->first_insert), lightfs_time_check(txn->first_insert, txn->last_insert), lightfs_time_check(txn->last_insert, txn->commit), lightfs_time_check(txn->commit, txn->free));
	} else {
		pr_info("[READ:%d]\n begin ~ wakeup: %d\n wakeup ~ create: %d\n create ~ commit: %d\n commit ~ free %d\n", txn->txn_id, lightfs_time_check(txn->begin, txn->wakeup), lightfs_time_check(txn->wakeup, txn->create), lightfs_time_check(txn->create, txn->commit), lightfs_time_check(txn->commit, txn->free));

	}
#endif
	//lightfs_error(__func__, "txn: %p\n", txn);
	kmem_cache_free(lightfs_txn_cachep, txn);
}

static inline void lightfs_txn_buf_init(void *txn_buf)
{
	DB_TXN_BUF *_txn_buf = txn_buf;
	memset(txn_buf, 0, sizeof(DB_TXN_BUF));
	INIT_LIST_HEAD(&_txn_buf->txn_buf_list);
	_txn_buf->is_deleted = 0;
	_txn_buf->is_rb = 0;
#ifdef TIME_CHECK
	lightfs_get_time(&_txn_buf->create);
#endif
}

static inline void lightfs_txn_buf_free(DB_TXN_BUF *txn_buf) {
#ifdef TIME_CHECK
	bool is_buffering = 0;
	char *str;
#endif
	if (txn_buf->key)
		kfree(txn_buf->key);
	if (txn_buf->buf) {
		if (txn_buf->type == LIGHTFS_META_SET) {
			kmem_cache_free(lightfs_meta_buf_cachep, txn_buf->buf);
		} else {
			kmem_cache_free(lightfs_buf_cachep, txn_buf->buf); // TMP
		}
	}
#ifdef TXN_BUFFER
	if (txn_buf->is_rb) {
		spin_lock_irqsave(&txn_hdlr->txn_spin, irqflags);
		lightfs_txn_buffer_del(&txn_hdlr->txn_buffer, txn_buf);
		spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
	}
#endif
#ifdef TIME_CHECK
	lightfs_get_time(&txn_buf->free);
	switch (txn_buf->type) {
		case LIGHTFS_META_SET:
			str = "META_SET";
			is_buffering = 1;
			break;
		case LIGHTFS_DATA_SET:
			str = "DATA_SET";
			is_buffering = 1;
			break;
		case LIGHTFS_DATA_SEQ_SET:
			str = "DATA_SEQ_SET";
			is_buffering = 1;
			break;
		case LIGHTFS_META_DEL:
			str = "META_DEL";
			is_buffering = 1;
			break;
		case LIGHTFS_DATA_DEL:
			str = "DATA_DEL";
			is_buffering = 1;
			break;
		case LIGHTFS_DATA_DEL_MULTI:
			str = "DATA_DEL_MULTI";
			is_buffering = 1;
			break;
		case LIGHTFS_DATA_UPDATE:
			str = "DATA_UPDATE";
			is_buffering = 1;
			break;

		case LIGHTFS_META_SYNC_SET:
			str = "META_SYNC_SET";
			is_buffering = 0;
			break;

		case LIGHTFS_META_GET:
			str = "META_GET";
			is_buffering = 0;
			break;
		case LIGHTFS_DATA_GET:
			str = "DATA_GET";
			is_buffering = 0;
			break;
		case LIGHTFS_GET_MULTI:
			str = "GET_MULTI";
			is_buffering = 0;
			break;
		case LIGHTFS_COMMIT:
			str = "COMMIT";
			is_buffering = 0;
			break;
		case LIGHTFS_META_CURSOR:
			str = "META CURSOR";
			is_buffering = 0;
			break;
		case LIGHTFS_DATA_SET_WB:
			str = "DATA_SET_WB";
			is_buffering = 1;
			break;
		default:
			break;
	}
	if (is_buffering) {
		//pr_info("[%.*s]\n create ~ queue: %d\n queue ~ insert: %d\n insert ~ transfer: %d\n transfer ~ complete: %d\n complete ~ free: %d\n", strlen(str), str, lightfs_time_check(txn_buf->create, txn_buf->queue), lightfs_time_check(txn_buf->queue, txn_buf->insert), lightfs_time_check(txn_buf->insert, txn_buf->transfer), lightfs_time_check(txn_buf->transfer, txn_buf->complete), lightfs_time_check(txn_buf->complete, txn_buf->free));
	} else {

		//pr_info("[%.*s]\n create ~ tranfer: %d\n transfer ~ complete: %d\n complete ~ free: %d\n", strlen(str), str, lightfs_time_check(txn_buf->create, txn_buf->transfer), lightfs_time_check(txn_buf->transfer, txn_buf->complete), lightfs_time_check(txn_buf->complete, txn_buf->free));

	}
#endif
	kmem_cache_free(lightfs_txn_buf_cachep, txn_buf);
}

static inline void lightfs_dbc_init(void *dbc)
{
	DBC *cursor = dbc;
	cursor->buf_len = 0;
	cursor->idx = 0;
	cursor->io_tag = -1;
}

static inline void lightfs_dbc_free(DBC *dbc)
{
	if (dbc->io_tag != -1) {
		cheeze_free_io(dbc->io_tag);
	}
	kmem_cache_free(lightfs_dbc_cachep, dbc);	
}



#if 0
static bool lightfs_bstore_txn_check(void)
{
	unsigned long irqflags;
	bool ret;
	spin_lock_irqsave(&txn_hdlr->txn_spin, irqflags);
	if (txn_hdlr->txn_cnt < HARD_TXN_LIMIT) {
		ret = true;
	} else {
		ret = false;
	}
	spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
	return ret;
}
#endif

int __lightfs_bstore_txn_begin(DB_TXN *parent, DB_TXN **txn, uint32_t flags)
{
#ifdef TXN_TIME_CHECK
	ktime_t begin, wakeup;
#endif
	unsigned long irqflags;

#ifdef TXN_TIME_CHECK
	lightfs_get_time(&begin);
#endif

	if (flags == TXN_READONLY) {
		*txn = kmem_cache_alloc(lightfs_txn_cachep, GFP_NOIO);
		lightfs_txn_init(*txn);
		spin_lock_irqsave(&txn_hdlr->txn_spin, irqflags);
		(*txn)->state = TXN_READ;
		(*txn)->txn_id = txn_hdlr->txn_id++;
		spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
		//lightfs_error(__func__, "READ txn: %p\n", *txn);
		return 0;
	}

#ifdef TXN_TIME_CHECK
	lightfs_get_time(&wakeup);
#endif

	*txn = kmem_cache_alloc(lightfs_txn_cachep, GFP_NOIO);
	lightfs_txn_init(*txn);
#ifdef TXN_TIME_CHECK
	memcpy(&((*txn)->begin), &begin, sizeof(ktime_t));
	memcpy(&((*txn)->wakeup), &wakeup, sizeof(ktime_t));
#endif

	if (flags == TXN_SYNC_WRITE) {
		(*txn)->state |= TXN_SYNC;
	}

	return 0;
}

void *lightfs_bstore_txn_get_cb(void *completionp)
{
	struct completion *cp = completionp;
	if (completion_done(cp)) {
		pr_info("asdlkjqwlekjzx,mcnasd\n");
	
	} else {
		complete(cp);
	}
	return NULL;
}

int lightfs_bstore_txn_get(DB *db, DB_TXN *txn, DBT *key, DBT *value, uint32_t off, enum lightfs_req_type type)
{
	DB_TXN_BUF *txn_buf;
	int ret = 0;
#ifdef TXN_BUFFER
	unsigned long irqflags;
#endif

	txn_buf = kmem_cache_alloc(lightfs_txn_buf_cachep, GFP_NOIO);
	lightfs_txn_buf_init(txn_buf);
	txn_buf->txn_id = txn->txn_id;
	txn_buf->db = db;
	txn_buf_setup(txn_buf, value->data, off, value->size, type);
	copy_txn_buf_key_from_dbt(txn_buf, key);

#ifdef TXN_BUFFER
	spin_lock_irqsave(&txn_hdlr->txn_spin, irqflags);
	if (lightfs_txn_buffer_get(&txn_hdlr->txn_buffer, txn_buf)) {
		lightfs_error(__func__, "Get Hit!\n");
		txn_buf->buf = NULL;
		spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
		lightfs_txn_buf_free(txn_buf);
		return 0;
	}
	spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
#endif
	
	txn_hdlr->db_io->get(db, txn_buf);
	
	txn_buf->buf = NULL;
	txn_buf->key = NULL;

	if (txn_buf->ret == DB_NOTFOUND) {
		ret = DB_NOTFOUND;
	} else {
		//value->size = txn_buf->ret;
	}

	lightfs_txn_buf_free(txn_buf);

	return ret;
}

int lightfs_bstore_txn_get_multi(DB *db, DB_TXN *txn, DBT *key, uint32_t cnt, YDB_CALLBACK_FUNCTION f, void *extra, enum lightfs_req_type type)
{
	DB_TXN_BUF *txn_buf;
	int ret = 0;
	char *buf = NULL;
	char *data_key = key->data;
	DBT value;
	int i, tmp = 0;
	uint64_t block_num = lightfs_data_key_get_blocknum(data_key, key->size);
#ifdef TXN_BUFFER
	uint64_t buffer_block_num = block_num;
	volatile bool is_partial = 0;
	unsigned long irqflags;
#endif

	txn_buf = kmem_cache_alloc(lightfs_txn_buf_cachep, GFP_NOIO);
	lightfs_txn_buf_init(txn_buf);
	txn_buf->txn_id = txn->txn_id;
	txn_buf->db = db;
	copy_txn_buf_key_from_dbt(txn_buf, key);

#ifdef TXN_BUFFER
	txn_buf_setup(txn_buf, buf, 0, PAGE_SIZE, type);
	for (i = 0; i < cnt; i++) {
		lightfs_data_key_set_blocknum(data_key, key->size, buffer_block_num++);
		spin_lock_irqsave(&txn_hdlr->txn_spin, irqflags);
		if (lightfs_txn_buffer_get(&txn_hdlr->txn_buffer, txn_buf)) {
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
			lightfs_error(__func__, "MGet Hit! buffer_block_num: %d\n", buffer_block_num - 1);
		} else {
			if (!is_partial) {
				is_partial = 1;
				block_num = buffer_block_num - 1;
				tmp = i;
			}
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
			lightfs_error(__func__, "MGet Miss! buffer_block_num: %d\n", buffer_block_num - 1);
			//lightfs_error(__func__, "Get Miss! buffer_block_num: %d\n", buffer_block_num - 1);
		}
		txn_buf->buf += PAGE_SIZE;
	}
#endif

	//pr_info("cnt: %d, block_num: %d\n", cnt, block_num);
	txn_buf_setup(txn_buf, buf, 0, cnt, type);
	lightfs_data_key_set_blocknum(data_key, key->size, block_num);
	txn_buf->buf -= (tmp * PAGE_SIZE);


	txn_hdlr->db_io->get_multi(db, txn_buf);
	
	if (txn_buf->ret == DB_NOTFOUND) {
		ret = DB_NOTFOUND;
		goto free_out;
	}

	buf = txn_buf->buf;
	for (i = 0; i < cnt; i++) {
		lightfs_data_key_set_blocknum(data_key, key->size, block_num++);
		dbt_setup(&value, buf + (i * PAGE_SIZE), PAGE_SIZE);
		f(key, &value, extra);
	}
	cheeze_free_io(txn_buf->ret);
free_out:
	txn_buf->buf = NULL;
	txn_buf->key = NULL;

	lightfs_txn_buf_free(txn_buf);

	return ret;
}

int lightfs_bstore_txn_get_multi_reada(DB *db, DB_TXN *txn, DBT *key, uint32_t cnt, void *extra, enum lightfs_req_type type)
{
	DB_TXN_BUF *txn_buf;
	int ret = 0;
	char *buf = NULL;
	char *data_key = key->data;
	int tmp = 0;
	uint64_t block_num = lightfs_data_key_get_blocknum(data_key, key->size);
#ifdef TXN_BUFFER
	uint64_t buffer_block_num = block_num;
	volatile bool is_partial = 0;
	unsigned long irqflags;
#endif

	txn_buf = kmem_cache_alloc(lightfs_txn_buf_cachep, GFP_NOIO);
	lightfs_txn_buf_init(txn_buf);
	txn_buf->txn_id = txn->txn_id;
	txn_buf->db = db;
	copy_txn_buf_key_from_dbt(txn_buf, key);

#ifdef TXN_BUFFER
	txn_buf_setup(txn_buf, buf, 0, PAGE_SIZE, type);
	for (i = 0; i < cnt; i++) {
		lightfs_data_key_set_blocknum(data_key, key->size, buffer_block_num++);
		spin_lock_irqsave(&txn_hdlr->txn_spin, irqflags);
		if (lightfs_txn_buffer_get(&txn_hdlr->txn_buffer, txn_buf)) {
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
			lightfs_error(__func__, "MGet Hit! buffer_block_num: %d\n", buffer_block_num - 1);
		} else {
			if (!is_partial) {
				is_partial = 1;
				block_num = buffer_block_num - 1;
				tmp = i;
			}
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
			lightfs_error(__func__, "MGet Miss! buffer_block_num: %d\n", buffer_block_num - 1);
			//lightfs_error(__func__, "Get Miss! buffer_block_num: %d\n", buffer_block_num - 1);
		}
		txn_buf->buf += PAGE_SIZE;
	}
#endif

	//lightfs_error(__func__, "block_num: %d, cnt: %d\n", block_num, cnt);
	txn_buf_setup(txn_buf, buf, 0, cnt, type);
	lightfs_data_key_set_blocknum(data_key, key->size, block_num);
	txn_buf->buf -= (tmp * PAGE_SIZE);


	txn_hdlr->db_io->get_multi_reada(db, txn_buf, extra);
	
	if (txn_buf->ret == DB_NOTFOUND) {
		ret = DB_NOTFOUND;
		goto free_out;
	}

free_out:
	txn_buf->buf = NULL;
	txn_buf->key = NULL;

	lightfs_txn_buf_free(txn_buf);

	return ret;
}

void *lightfs_bstore_txn_sync_put_cb(void *completionp)
{
	complete((struct completion *)completionp);
	return NULL;
}


int lightfs_bstore_txn_sync_put(DB *db, DB_TXN *txn, DBT *key, DBT *value, uint32_t off, enum lightfs_req_type type) {
	DB_TXN_BUF *txn_buf;

	txn_buf = kmem_cache_alloc(lightfs_txn_buf_cachep, GFP_NOIO);
	lightfs_txn_buf_init(txn_buf);
	txn_buf->txn_id = txn->txn_id;
	txn_buf->db = db;
	txn_buf->buf = (char*)kmem_cache_alloc(lightfs_meta_buf_cachep, GFP_NOIO);
	txn_buf_setup_cpy(txn_buf, value->data, off, value->size, type);
	txn_buf->len = PAGE_SIZE;
	alloc_txn_buf_key_from_dbt(txn_buf, key);

	txn_hdlr->db_io->sync_put(db, txn_buf);
	kmem_cache_free(lightfs_meta_buf_cachep, txn_buf->buf);

	txn_buf->type = LIGHTFS_COMMIT;
	txn_hdlr->db_io->commit(txn_buf);

	txn_buf->buf = NULL;
	lightfs_txn_buf_free(txn_buf);

	return 0;
}

void *lightfs_bstore_dbc_cb(void *completionp)
{
	complete((struct completion *)completionp);
	return NULL;
}

int lightfs_bstore_dbc_c_get(DBC *dbc, DBT *key, DBT *value, uint32_t flags)
{
	uint16_t size;
	int ret = DB_NOTFOUND;
	DB_TXN_BUF *txn_buf = (DB_TXN_BUF *)dbc->extra;
	//print_key(__func__, key->data, key->size);
	if (dbc->idx >= dbc->buf_len) {
		if (flags == DB_SET_RANGE) {
			txn_buf->off = 1;
		} else {
			txn_buf->off = 0;
		}
		txn_buf->len = value->size;
		if (value->size == 0) {
			return 0;
		}
		copy_txn_buf_key_from_dbt(txn_buf, key);
		if (dbc->io_tag != -1) {
			cheeze_free_io(dbc->io_tag);
			dbc->io_tag = -1;
		}
		ret = txn_hdlr->db_io->iter(dbc->dbp, dbc, txn_buf); 
		dbc->idx = 0;
#ifdef CHEEZE
		return dbc->cheeze_dbc->c_get(dbc->cheeze_dbc, key, value, flags);
#endif
		if (txn_buf->ret == DB_NOTFOUND) {
			dbc->io_tag = ret;
			return DB_NOTFOUND;
		} else {
			dbc->io_tag = ret;
			dbc->buf_len = txn_buf->ret;
			dbc->buf = txn_buf->buf;
		}
		return 0; //!!!!!!!!!!!!
	}
	if (flags == DB_SET_RANGE) {
		if (dbc->idx != 0) {
			return 0;
		} else {
			BUG_ON(dbc->buf == NULL);
			dbc->idx += copy_dbt_from_dbc(dbc, key);
			dbc->idx += copy_value_dbt_from_dbc(dbc, value);
			return 0;
		}
	}
	//TODO end-of-iter
	size = dbc_get_size(dbc);
	if (size == 0) {
		dbc->idx += sizeof(size);
		return DB_NOTFOUND;
	} else if (size == 1) {
		dbc->idx += sizeof(size);
	} else {
		BUG_ON(dbc->buf == NULL);
		dbc->idx += copy_dbt_from_dbc(dbc, key);
		dbc->idx += copy_value_dbt_from_dbc(dbc, value);
	}

	return 0;
}

//greater or equal
int lightfs_bstore_dbc_c_getf_set_range(DBC *dbc, uint32_t flags, DBT *key, YDB_CALLBACK_FUNCTION f, void *extra)
{
	uint32_t idx = 0;
	DB_TXN_BUF *txn_buf = (DB_TXN_BUF *)dbc->extra;
	if (dbc->idx >= dbc->buf_len) {
		txn_buf->off = 1;
		txn_buf->len = flags;
		copy_txn_buf_key_from_dbt(txn_buf, key);
		txn_hdlr->db_io->iter(dbc->dbp, dbc, txn_buf); 
		dbc->idx = 0;
#ifdef CHEEZE
		return dbc->cheeze_dbc->c_getf_set_range(dbc->cheeze_dbc, flags, key, f, extra);
#endif
		if (txn_buf->ret == DB_NOTFOUND) {
			return DB_NOTFOUND;
		}
	}
	//TODO end-of-iter
	idx = copy_dbt_from_dbc(dbc, &dbc->key);
	if (idx == 0) {
		return DB_NOTFOUND;
	}
	dbc->idx += idx;
	dbc->idx += copy_value_dbt_from_dbc(dbc, &dbc->value);
	f(&dbc->key, &dbc->value, extra);

	return 0;
}

int lightfs_bstore_dbc_c_getf_next(DBC *dbc, uint32_t flags, YDB_CALLBACK_FUNCTION f, void *extra)
{
	uint32_t idx = 0;
	DB_TXN_BUF *txn_buf = (DB_TXN_BUF *)dbc->extra;

	// NEXT, SET_RANGE
	if (dbc->idx >= dbc->buf_len) {
		DBT key;
		char *str = "asd";
		key.data = str;
		key.size = strlen(str);
		txn_buf->off = 0;
		txn_buf->len = flags;
		copy_txn_buf_key_from_dbt(txn_buf, &key);
		txn_hdlr->db_io->iter(dbc->dbp, dbc, txn_buf); 
		dbc->idx = 0;
#ifdef CHEEZE
		return dbc->cheeze_dbc->c_getf_next(dbc->cheeze_dbc, flags, f, extra);
#endif
		if (txn_buf->ret == DB_NOTFOUND) {
			return DB_NOTFOUND;
		}
	}
	//TODO end-of-iter
	idx = copy_dbt_from_dbc(dbc, &dbc->key);
	if (idx == 0) {
		return DB_NOTFOUND;
	}
	dbc->idx += idx;
	dbc->idx += copy_value_dbt_from_dbc(dbc, &dbc->value);
	f(&dbc->key, &dbc->value, extra);

	return 0;
}

int lightfs_bstore_dbc_close(DBC *dbc)
{
	DB_TXN_BUF *txn_buf = (DB_TXN_BUF *)dbc->extra;

#ifdef CHEEZE
	dbc->cheeze_dbc->c_close(dbc->cheeze_dbc);
#endif

	txn_buf->buf = NULL;
	txn_buf->key = NULL;
	lightfs_txn_buf_free(txn_buf);
	lightfs_dbc_free(dbc);

	return 0;
}

int lightfs_bstore_dbc_cursor(DB *db, DB_TXN *txn, DBC **dbc, enum lightfs_req_type type)
{
	DB_TXN_BUF *txn_buf;
	DBC *cursor;

	cursor = *dbc = kmem_cache_alloc(lightfs_dbc_cachep, GFP_NOIO);
	lightfs_dbc_init(cursor);
	txn_buf = kmem_cache_alloc(lightfs_txn_buf_cachep, GFP_NOIO);
	lightfs_txn_buf_init(txn_buf);
	txn_buf->txn_id = txn->txn_id;

	txn_buf_setup(txn_buf, cursor->buf, 0, 0, type);
	cursor->extra = (void *)txn_buf;
	cursor->c_get = lightfs_bstore_dbc_c_get;
	cursor->c_getf_set_range = lightfs_bstore_dbc_c_getf_set_range;
	cursor->c_getf_next = lightfs_bstore_dbc_c_getf_next;
	cursor->c_close = lightfs_bstore_dbc_close;
	cursor->dbp = db;

#ifdef CHEEZE
	db_cursor(db, txn, &cursor->cheeze_dbc, 0);
#endif
	
	return 0;
}


int lightfs_bstore_txn_insert(DB *db, DB_TXN *txn, const DBT *key, const DBT *value, uint32_t off, enum lightfs_req_type type)
{
	DB_TXN_BUF *txn_buf;
#ifdef TXN_BUFFER
	DB_TXN_BUF *old_txn_buf;
	unsigned long irqflags;
#endif

	txn_buf = kmem_cache_alloc(lightfs_txn_buf_cachep, GFP_NOIO);
	lightfs_txn_buf_init(txn_buf);
	txn_buf->txn_id = txn->txn_id;
	txn_buf->db = db;
	txn_buf->txn = txn;
	alloc_txn_buf_key_from_dbt(txn_buf, key);

	
	if (value) { // SET, SEQ_SET, UPDATE
		if (type == LIGHTFS_META_SET) {
			txn_buf->buf = (char*)kmem_cache_alloc(lightfs_meta_buf_cachep, GFP_NOIO);	
		} else if (type == LIGHTFS_DATA_SET_WB) {
			txn_buf->buf = value->data;
		} else {
			txn_buf->buf = (char*)kmem_cache_alloc(lightfs_buf_cachep, GFP_NOIO); // TMP
		}

		if (value->size == 0) { // SET, SEQ_SET
			txn_buf->type = type;
			txn_buf->off = off;
			txn_buf->len = PAGE_SIZE;
			txn_buf->update = PAGE_SIZE - off;
			memset(txn_buf->buf + off, 0, PAGE_SIZE - off);
		} else if (type == LIGHTFS_DATA_SET_WB) {
			txn_buf->type = type;
			txn_buf->off = 0;
		} else { // UPDATE
			txn_buf_setup_cpy(txn_buf, value->data, off, value->size, type);
			txn_buf->update = value->size;
		}

		txn_buf->len = 4096;
#ifdef TXN_BUFFER
		spin_lock_irqsave(&txn_hdlr->txn_spin, irqflags);
		if ( (old_txn_buf = lightfs_txn_buffer_put(&txn_hdlr->txn_buffer, txn_buf)) ) {
			// txn_buf_free
			if (old_txn_buf != txn_buf) {
				// txn_buf is absorbed
				old_txn_buf->is_rb = 0;
				txn_buf->is_rb = 1;
				spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
				lightfs_error(__func__, "Put Buffer Hit: Delayed\n");
			} else {
				// txn_buf is canceled
				spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
				lightfs_txn_buf_free(txn_buf);
				lightfs_error(__func__, "Put Buffer Hit: Canceled\n");
				return 0;
			}
		} else {
			txn_buf->is_rb = 1;
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
		}
#endif
	} else { // DEL, DEL_MULTI ==> off: cnt of objects
		txn_buf_setup(txn_buf, NULL, off, PAGE_SIZE * off, type);
		txn->cnt++;
		txn->size += calc_txn_buf_size(txn_buf);
		list_add_tail(&txn_buf->txn_buf_list, &txn->txn_buf_list);
		//pr_info("txn_id: %d off %d txn_buf: %d txn->cnt: %d txn->size %d\n", txn->txn_id, off, calc_txn_buf_size(txn_buf), txn->cnt, txn->size);
		return 0;
	}

	txn->cnt++;
	txn->size += calc_txn_buf_size(txn_buf);
	list_add_tail(&txn_buf->txn_buf_list, &txn->txn_buf_list);
#ifdef TIME_CHECK
	lightfs_get_time(&txn_buf->queue);
#endif
#ifdef TXN_TIME_CHECK
	if (txn_buf->txn->is_inserted) {
		lightfs_get_time(&txn_buf->txn->last_insert);
	} else {
		lightfs_get_time(&txn_buf->txn->first_insert);
		lightfs_get_time(&txn_buf->txn->last_insert);
	}
#endif

	return 0;
}

int lightfs_bstore_txn_commit(DB_TXN *txn, uint32_t flags)
{
	unsigned long irqflags;

	spin_lock_irqsave(&txn_hdlr->txn_spin, irqflags);

	if (!txn->cnt) {
		lightfs_txn_free(txn);
		spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
		return 0;
	}

	if (txn->state & TXN_READ) {
#ifdef TXN_TIME_CHECK
		lightfs_get_time(&txn->commit);
#endif
		spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
		lightfs_txn_free(txn);
	} else if (txn->state & TXN_SYNC) {
		struct completion c;
	       	init_completion(&c);
		txn->completionp = &c;
		
		txn_hdlr->txn_cnt++;
		txn->txn_id = txn_hdlr->txn_id++;
		list_add_tail(&(txn->txn_list), &txn_hdlr->sync_txn_list);

		txn->state |= TXN_COMMITTED;
		txn_hdlr->syncing_cnt++;
		if (wq_has_sleeper(&txn_hdlr->wq)) {
			//lightfs_error(__func__, "Waking up the handler\n");
			wake_up(&txn_hdlr->wq);
		}
		spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
		wait_for_completion(&c);
	} else {
		txn_hdlr->txn_cnt++;
		txn->txn_id = txn_hdlr->txn_id++;
		list_add_tail(&(txn->txn_list), &txn_hdlr->txn_list);

		txn->state |= TXN_COMMITTED;
#ifdef TXN_TIME_CHECK
		lightfs_get_time(&txn->commit);
#endif
		spin_unlock_irqrestore(&txn_hdlr->txn_spin, irqflags);
	}
	return 0;
}

int lightfs_bstore_txn_abort(DB_TXN *txn)
{
	return 0;
}

int lightfs_bstore_txn_free(DB_TXN *txn)
{
	return 0;
}

static int lightfs_c_txn_create(DB_C_TXN **c_txn, enum lightfs_txn_state state, int workq_id, bool is_new)
{
	unsigned long flag;
	*c_txn = kmem_cache_alloc(lightfs_c_txn_cachep, GFP_NOIO);

	lightfs_c_txn_init(*c_txn);

	spin_lock_irqsave(&txn_hdlr->txn_spin, flag);
	if (state & TXN_ORDERED) {
		list_add_tail(&((*c_txn)->c_txn_list), &txn_hdlr->ordered_c_txn_list);
		txn_hdlr->ordered_c_txn_cnt++;
	} else {
		list_add_tail(&((*c_txn)->c_txn_list), &txn_hdlr->orderless_c_txn_list);
		txn_hdlr->orderless_c_txn_cnt++;
	}
	(*c_txn)->state = state;
	(*c_txn)->workq_id = workq_id;
	spin_unlock_irqrestore(&txn_hdlr->txn_spin, flag);

	return 0;
}

static int lightfs_c_txn_destroy(DB_C_TXN *c_txn)
{
	DB_TXN_BUF *txn_buf;
	DB_TXN *txn;
	unsigned long flag;

	spin_lock_irqsave(&txn_hdlr->txn_spin, flag);
	list_del(&c_txn->c_txn_list);
	spin_unlock_irqrestore(&txn_hdlr->txn_spin, flag);

	while (!list_empty(&c_txn->txn_list)) {
		txn = list_first_entry(&c_txn->txn_list, DB_TXN, txn_list);
		while(!list_empty(&txn->txn_buf_list)) {
			txn_buf = list_first_entry(&txn->txn_buf_list, DB_TXN_BUF, txn_buf_list);
			list_del(&txn_buf->txn_buf_list);
			lightfs_txn_buf_free(txn_buf);
		}
		list_del(&txn->txn_list);
		lightfs_txn_free(txn);
	}

	lightfs_c_txn_free(c_txn);
	return 0;
}

static int lightfs_c_txn_insert(DB_C_TXN *c_txn, DB_TXN *txn)
{
	DB_TXN_BUF *txn_buf;

	list_for_each_entry(txn_buf, &txn->txn_buf_list, txn_buf_list) {
#ifdef TIME_CHECK
		lightfs_get_time(&txn_buf->insert);
#endif
		bloomfilter_set(c_txn->filter, txn_buf->key, txn_buf->key_len);
	}
	txn->state |= TXN_TRANSFERING;

	if (txn_hdlr->txn_cnt == 0) {
		WARN_ON(1);
		lightfs_error(__func__, "TXN CNT: %ld, c_txn->size:%d, c_txn->state: %d\n", txn_hdlr->txn_cnt, c_txn->size, c_txn->state);
	}
	


	if (txn->cnt == 0) {
		list_del(&txn->txn_list);
		txn_hdlr->txn_cnt--;
		BUG_ON(txn_hdlr->txn_cnt > 10000000);
		lightfs_txn_free(txn);	
		return 0;
	} else {
		list_move_tail(&txn->txn_list, &c_txn->txn_list);
		txn_hdlr->txn_cnt--;
		
	}
	c_txn->size += txn->size;
	c_txn->cnt += txn->cnt;


	return 0;
}

#if 0
static int lightfs_c_txn_make_relation(DB_C_TXN *existing_c_txn, DB_C_TXN *c_txn)
{
	DB_C_TXN_LIST *child;
	c_txn_list_alloc(&child, c_txn);
	c_txn->parents++;
	list_add_tail(&child->c_txn_list, &existing_c_txn->children);

	return 0;
}
#endif


int lightfs_bstore_c_txn_commit_flush(DB_C_TXN *c_txn) {
	unsigned long flag;
	spin_lock_irqsave(&txn_hdlr->txn_spin, flag);
	if (c_txn->state & TXN_ORDERED) {
		txn_hdlr->ordered_c_txn_cnt -= c_txn->committing_cnt;
	} else {
		txn_hdlr->orderless_c_txn_cnt -= c_txn->committing_cnt;
	}
	spin_unlock_irqrestore(&txn_hdlr->txn_spin, flag);
	return 0;
}

static void lightfs_c_txn_commit_flush_work(struct work_struct *work) {
	DB_C_TXN *c_txn = container_of(work, DB_C_TXN, commit_work);

	flush_workqueue(txn_hdlr->workqs[c_txn->workq_id]);
	lightfs_bstore_c_txn_commit_flush(c_txn);
	lightfs_queue_push(txn_hdlr->workq_tags, (void*)(c_txn->workq_id));
	lightfs_c_txn_destroy(c_txn);
}


static int lightfs_c_txn_commit(DB_C_TXN *c_txn)
{
	DB_C_TXN_LIST *child;
	DB_C_TXN *child_c_txn;


	while (!list_empty(&c_txn->children)) {
		child = list_first_entry(&c_txn->children, DB_C_TXN_LIST, c_txn_list);
		child_c_txn = child->c_txn_ptr;
		child_c_txn->parents--;
		if (!child_c_txn->parents) {
			list_move_tail(&child_c_txn->c_txn_list, &txn_hdlr->orderless_c_txn_list);
			txn_hdlr->orderless_c_txn_cnt++;
			txn_hdlr->ordered_c_txn_cnt--;
		}
		list_del(&child->c_txn_list);
		c_txn_list_free(child);
	}

	lightfs_c_txn_destroy(c_txn);

	return 0;
}

static void lightfs_c_txn_commit_work(struct work_struct *work) {
	DB_C_TXN *c_txn = container_of(work, DB_C_TXN, work);

	lightfs_c_txn_commit(c_txn);
}

static void* lightfs_c_txn_transfer_cb(void *data) {
	//DB_C_TXN_LIST *committed_c_txn_list;
	DB_C_TXN *c_txn = (DB_C_TXN *)data;

	//if (c_txn->state & TXN_FLUSH || c_txn->state & TXN_ORDERED) {
	if (c_txn->state & TXN_FLUSH) {
		lightfs_bstore_c_txn_commit_flush(c_txn); // blocking commit flush // TODO
	}

	if (c_txn->state & TXN_ORDERED) {
		uint16_t cnt = 0;
		DB_TXN *txn;
		list_for_each_entry(txn, &c_txn->txn_list, txn_list) {
			cnt++;
			complete(txn->completionp);
		}
		spin_lock(&txn_hdlr->txn_spin);
		txn_hdlr->syncing_cnt -= cnt;
		txn_hdlr->sync_c_txn = NULL;
		spin_unlock(&txn_hdlr->txn_spin);
		lightfs_bstore_c_txn_commit_flush(c_txn); // blocking commit flush // TODO
		lightfs_c_txn_commit(c_txn);
		return NULL;
	}

	if (txn_hdlr->commit_workq) {
		queue_work(txn_hdlr->commit_workq, &c_txn->work);
	} else {
		lightfs_c_txn_commit(c_txn);
	}
	return NULL;
}

static int lightfs_c_txn_transfer(DB_C_TXN *c_txn)
{
	//TODO: send c_txn & add 
	//
	txn_hdlr->committing_c_txn_cnt++;
	c_txn->state |= TXN_TRANSFERING;
	txn_hdlr->db_io->transfer(NULL, c_txn, lightfs_c_txn_transfer_cb, c_txn); // should block or sleep until transfer is completed

	return 0;
}

static void lightfs_c_txn_transfer_work(struct work_struct *work) {
	DB_C_TXN *c_txn = container_of(work, DB_C_TXN, transfer_work);
	lightfs_c_txn_transfer(c_txn);
}


void *lightfs_bstore_c_txn_commit_flush_cb(void *completionp)
{
	complete((struct completion *)completionp);
	return NULL;
}

#if 0
/* 
 * we merge txn to the "merge_c_txn", if possible. 
 * if not, create a new c_txn after the "related_c_txn"
 * */
static enum lightfs_c_txn_state lightfs_txn_calc_order(DB_TXN *txn, DB_C_TXN **merge_c_txn, DB_C_TXN **related_c_txn)
{
	DB_C_TXN *c_txn;
	DB_TXN_BUF *txn_buf;
	//int relations = 0;
	int best_diff = C_TXN_LIMIT_BYTES + 1;
	int diff = 0;
	enum lightfs_c_txn_state ret = C_TXN_ORDERLESS;
	DB_C_TXN *best_c_txn = NULL, *target_c_txn = NULL;
	
	//TODO: Add parent

	/*
	if (txn_hdlr->orderless_c_txn_cnt == 0) {
		return 0; // empty
	}
	*/
	
	list_for_each_entry_reverse(c_txn, &txn_hdlr->ordered_c_txn_list, c_txn_list) {
		if (c_txn->state == TXN_TRANSFERING) {
			continue;
		}
		diff = diff_c_txn_and_txn(c_txn, txn);
		if ((diff >= 0) && (best_diff > diff)) {
			best_c_txn = c_txn; // the file that is able to be merged
		}
		list_for_each_entry(txn_buf, &txn->txn_buf_list, txn_buf_list) {
			if (bloomfilter_get(c_txn->filter, txn_buf->key, txn_buf->key_len)) {
				//relations++;
				target_c_txn = c_txn; // related file
				ret = C_TXN_ORDERED;
				goto out;
			}
		}
	}

	best_diff = C_TXN_LIMIT_BYTES + 1;
	best_c_txn = NULL;
	list_for_each_entry(c_txn, &txn_hdlr->orderless_c_txn_list, c_txn_list) {
		if (c_txn->state == TXN_TRANSFERING) {
			continue;
		}
		diff = diff_c_txn_and_txn(c_txn, txn);
		if ((diff >= 0) && (best_diff > diff)) {
			best_c_txn = c_txn; // the file that is able to be merged
		}
		list_for_each_entry(txn_buf, &txn->txn_buf_list, txn_buf_list) {
			if (bloomfilter_get(c_txn->filter, txn_buf->key, txn_buf->key_len)) {
				//relations++;
				target_c_txn = c_txn; // related file
				ret = C_TXN_ORDERED;
				goto out;
			}
		}
	}

out:
	*merge_c_txn = best_c_txn;
	*related_c_txn = target_c_txn;
	return ret;
}
#endif

#ifdef TXN_BUFFER
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


static DB_TXN_BUF *find_val_with_key(struct rb_root *root, char *key, uint16_t key_len)
{
	struct rb_node *node = root->rb_node;
	DB_TXN_BUF *tmp;
	int result;

	while (node) {
		tmp = container_of(node, DB_TXN_BUF, rb_node);
		result = lightfs_keycmp(key, key_len, tmp->key, tmp->key_len);

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

static int txn_buffer_insert(struct rb_root *root, DB_TXN_BUF *node)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	DB_TXN_BUF *this;
	int result;

	while (*new) {
		this = container_of(*new, DB_TXN_BUF, rb_node);
		result = lightfs_keycmp(node->key, node->key_len, this->key, this->key_len);
		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return -1;
	}

	rb_link_node(&node->rb_node, parent, new);
	rb_insert_color(&node->rb_node, root);

	return 0;
}

static int lightfs_txn_buffer_get(struct rb_root *root, DB_TXN_BUF *txn_buf)
{
	DB_TXN_BUF *node;

	node = find_val_with_key(root, txn_buf->key, txn_buf->key_len);
	if (node == NULL) {
		return 0;
	}
	if (node->is_deleted) {
		memset(txn_buf->buf, 0, txn_buf->len);
		return 0;
	}
	memcpy(txn_buf->buf, node->buf, txn_buf->len);

	return txn_buf->len;
}

static DB_TXN_BUF *lightfs_txn_buffer_put(struct rb_root *root, DB_TXN_BUF *txn_buf)
{
	DB_TXN_BUF *node;
	int ret;

	node = find_val_with_key(root, txn_buf->key, txn_buf->key_len);
	if (node != NULL) {
		if (node->type != txn_buf->type) {
			lightfs_error(__func__, "txn_buf->type: %d, old_txn_buf->type :%d\n", txn_buf->type, node->type);
		}
		if (!(node->txn->state & TXN_TRANSFERING)) {
			memcpy(node->buf, txn_buf->buf + txn_buf->off, txn_buf->len);
			node->off = txn_buf->off;
			node->len = txn_buf->len;
			node->update = txn_buf->update;
			node->type = txn_buf->type;
			return txn_buf;
		} else {
			rb_replace_node(&node->rb_node, &txn_buf->rb_node, root);
			return node;
		}
	}
	ret = txn_buffer_insert(root, txn_buf);
	BUG_ON(ret != 0);
	return NULL;
}

static int lightfs_txn_buffer_del(struct rb_root *root, DB_TXN_BUF *txn_buf) {
	rb_erase(&txn_buf->rb_node, root);
	return 0;
}
#endif

static bool lightfs_txn_hdlr_check_state(void)
{
	volatile bool ret = false;
	volatile uint32_t cnt = 0;
	spin_lock(&txn_hdlr->txn_spin);
	//if (txn_hdlr->txn_cnt >= SOFT_TXN_LIMIT/4) {
	cnt = txn_hdlr->txn_cnt;
	//if (cnt > SOFT_TXN_LIMIT / 10) {
	if (cnt > 320) {
		ret = true;
	} else {
		if (txn_hdlr->syncing_cnt) {
			ret = true;
		} else {
			if (!list_empty(&txn_hdlr->sync_txn_list)) {
				ret = true;
			} else if (txn_hdlr->sync_c_txn) {
				ret = true;
			}
		}
	}
	spin_unlock(&txn_hdlr->txn_spin);
	return ret;
}

// TODO:: READ????
int lightfs_txn_hdlr_run(void *data)
{
	DB_C_TXN *c_txn;
	DB_TXN *txn, *sync_txn; 
	int ret;
	unsigned long flags;

	while (1) {
		if (kthread_should_stop()) {
			spin_lock_irqsave(&txn_hdlr->txn_spin, flags);
			if ((!txn_hdlr->txn_cnt) && list_empty(&txn_hdlr->sync_txn_list) && list_empty(&txn_hdlr->txn_list)) {
				spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
				pr_info("txn handler stop");
				break;
			}
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
		}

txn_repeat:
		//TODO:: fsync: 1st priority
		/*
		c_txn_state = lightfs_txn_calc_order(txn, &merge_c_txn, &related_c_txn);
		if (merge_c_txn) {
			lightfs_c_txn_insert(merge_c_txn, txn);
		} else {
			lightfs_c_txn_create(&c_txn, c_txn_state);
			lightfs_c_txn_insert(c_txn, txn);
			lightfs_c_txn_make_relation(related_c_txn, c_txn);
		}
		*/

		/*
		if (txn_hdlr->txn_cnt < TXN_THRESHOLD && wq_has_sleeper(&txn_hdlr->txn_wq)) {
			lightfs_error(__func__, "touch TXN_THRESHOLD\n");
			wake_up_all(&txn_hdlr->txn_wq);
		}
		*/

process_sync:
		spin_lock_irqsave(&txn_hdlr->txn_spin, flags);
		if (list_empty(&txn_hdlr->sync_txn_list)) {
			goto transfer_sync;
		}
		sync_txn = list_first_entry(&txn_hdlr->sync_txn_list, DB_TXN, txn_list);
		if (!(sync_txn->state & TXN_COMMITTED)) {
			//goto txn_repeat;
			goto skip_sync;
		}
		if (txn_hdlr->sync_c_txn) { // running sync_c_txn exists
			if (txn_hdlr->sync_c_txn->state & TXN_TRANSFERING) { // syncing is on going
				goto skip_sync;
			} else { // still enough space in sync_c_txn
				lightfs_c_txn_insert(txn_hdlr->sync_c_txn, sync_txn);
			}
		} else { // create new sync_c_txn
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
			lightfs_c_txn_create(&txn_hdlr->sync_c_txn, TXN_ORDERED, txn_hdlr->current_workq_id, 1);	
			spin_lock_irqsave(&txn_hdlr->txn_spin, flags);
			txn_hdlr->sync_c_txn->committing_cnt = 1;
			txn_hdlr->sync_c_txn->txn_id = sync_txn->txn_id;
			lightfs_c_txn_insert(txn_hdlr->sync_c_txn, sync_txn);
		}
		// after c_txn_insert & the txn_spin is unlocked
		spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
		goto process_sync;

transfer_sync:
		if ( (txn_hdlr->sync_c_txn && !(txn_hdlr->sync_c_txn->state & TXN_TRANSFERING)) ) {
			//spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
			txn_hdlr->sync_c_txn->state |= TXN_TRANSFERING;
			queue_work(txn_hdlr->workqs[0], &txn_hdlr->sync_c_txn->transfer_work);
		}

skip_sync:
		if (list_empty(&txn_hdlr->txn_list)) {
			goto transfer_now;
		}

		txn = list_first_entry(&txn_hdlr->txn_list, DB_TXN, txn_list);
		if (!(txn->state & TXN_COMMITTED)) {
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
			//goto txn_repeat;
			goto wait_for_txn;
		}

		if (txn_hdlr->ordered_c_txn_cnt + txn_hdlr->orderless_c_txn_cnt > C_TXN_COMMITTING_LIMIT) {
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
			goto txn_repeat;
		}

		if (txn_hdlr->running_c_txn) {
			if (diff_c_txn_and_txn(txn_hdlr->running_c_txn, txn) < 0) { // transfer
				//lightfs_error(__func__, "txn_cnt: %d\n", txn_hdlr->txn_cnt)
				spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
				lightfs_c_txn_transfer(txn_hdlr->running_c_txn);
				spin_lock_irqsave(&txn_hdlr->txn_spin, flags);
				txn_hdlr->running_c_txn = NULL;
			} else { // can be merge
				lightfs_c_txn_insert(txn_hdlr->running_c_txn, txn);
			}
		} else {
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
			lightfs_c_txn_create(&c_txn, TXN_ORDERLESS, txn_hdlr->current_workq_id, 1);
			spin_lock_irqsave(&txn_hdlr->txn_spin, flags);
			if (txn_hdlr->running_c_txn_id == 0) {
				txn_hdlr->running_c_txn_id = txn->txn_id;
				txn_hdlr->current_workq_id = 0;
			}
			c_txn->txn_id = txn_hdlr->running_c_txn_id;
			lightfs_c_txn_insert(c_txn, txn);
			txn_hdlr->running_c_txn_cnt++;
			txn_hdlr->running_c_txn = c_txn;
			if (txn_hdlr->running_c_txn_cnt >= RUNNING_C_TXN_LIMIT) {
				//if (txn_hdlr->running_c_txn_cnt == 0) {}
				c_txn->committing_cnt = txn_hdlr->running_c_txn_cnt;
				c_txn->state |= TXN_FLUSH;
				txn_hdlr->running_c_txn_id = 0;
				txn_hdlr->running_c_txn_cnt = 0;
			}
		}
		goto skip_sync;

transfer_now:
		// may sleep thread, if transfering txn is full
		// transfer a txn that have most children
		if (txn_hdlr->running_c_txn) {
			if (txn_hdlr->running_c_txn->committing_cnt == 0) {
				txn_hdlr->running_c_txn->committing_cnt = txn_hdlr->running_c_txn_cnt;
			}
			txn_hdlr->running_c_txn->state |= TXN_FLUSH;
			txn_hdlr->running_c_txn_id = 0;
			txn_hdlr->running_c_txn_cnt = 0;
			spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
			lightfs_c_txn_transfer(txn_hdlr->running_c_txn);
			txn_hdlr->running_c_txn = NULL;
		} else {

			spin_unlock_irqrestore(&txn_hdlr->txn_spin, flags);
		}

wait_for_txn:
		//cond_resched();
		ret = wait_event_interruptible_timeout(txn_hdlr->wq, kthread_should_stop() || lightfs_txn_hdlr_check_state(), msecs_to_jiffies(TXN_FLUSH_TIME));
	}
	return 0;
}

int lightfs_txn_hdlr_init(void)
{
	int ret, i;

	txn_hdlr_alloc(&txn_hdlr);
	
	lightfs_c_txn_cachep = kmem_cache_create("lightfs_c_txn", sizeof(DB_C_TXN), 0, KMEM_CACHE_FLAG, NULL);

	if (!lightfs_c_txn_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize c txn cache.\n");
		ret = -ENOMEM;
		goto out_free_c_txn_cachep;
	}


	lightfs_txn_cachep = kmem_cache_create("lightfs_txn", sizeof(DB_TXN), 0, KMEM_CACHE_FLAG, NULL);

	if (!lightfs_txn_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize txn cache.\n");
		ret = -ENOMEM;
		goto out_free_txn_cachep;
	}

	lightfs_txn_buf_cachep = kmem_cache_create("lightfs_txn_buf", sizeof(DB_TXN_BUF), 0, KMEM_CACHE_FLAG, NULL);

	if (!lightfs_txn_buf_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize txn buffer cache.\n");
		ret = -ENOMEM;
		goto out_free_txn_buf_cachep;
	}

	lightfs_buf_cachep = kmem_cache_create("lightfs_buf", PAGE_SIZE, 0, KMEM_CACHE_FLAG, NULL);

	if (!lightfs_buf_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize buffer cache.\n");
		ret = -ENOMEM;
		goto out_free_buf_cachep;
	}

	lightfs_meta_buf_cachep = kmem_cache_create("lightfs_meta_buf", INODE_SIZE, 0, KMEM_CACHE_FLAG, NULL);

	if (!lightfs_meta_buf_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize buffer cache.\n");
		ret = -ENOMEM;
		goto out_free_meta_buf_cachep;
	}

	lightfs_dbc_cachep = kmem_cache_create("lightfs_dbc", sizeof(DBC), 0, KMEM_CACHE_FLAG, NULL);

	if (!lightfs_dbc_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize dbc cache.\n");
		ret = -ENOMEM;
		goto out_free_dbc_cachep;
	}

	lightfs_dbc_buf_cachep = kmem_cache_create("lightfs_dbc_buf", ITER_BUF_SIZE, 0, KMEM_CACHE_FLAG, NULL);

	if (!lightfs_dbc_buf_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize dbc cache.\n");
		ret = -ENOMEM;
		goto out_free_dbc_buf_cachep;
	}

	txn_hdlr->workqs = kzalloc(sizeof(struct workqueue_struct *) * CONCURRENT_CNT, GFP_KERNEL);
	lightfs_queue_init(&txn_hdlr->workq_tags, CONCURRENT_CNT);
	for (i = 0; i < CONCURRENT_CNT; i++) {
		if (!(txn_hdlr->workqs[i] = alloc_workqueue("tranfer_queue", WQ_MEM_RECLAIM | WQ_UNBOUND, 0))) {
			lightfs_error(__func__, "workqueue failed\n");
		}
		lightfs_queue_push(txn_hdlr->workq_tags, (void*)((uint64_t)i));
	}
	txn_hdlr->commit_workq = alloc_workqueue("commit_queue", WQ_MEM_RECLAIM | WQ_UNBOUND, 0);

	lightfs_error(__func__, "lightfs_io_create\n");
	lightfs_io_create(&txn_hdlr->db_io);

	txn_hdlr->tsk = (struct task_struct *)kthread_run(lightfs_txn_hdlr_run, NULL, "lightfs_txn_hdlr");

	return 0;

out_free_dbc_buf_cachep:
	kmem_cache_destroy(lightfs_dbc_buf_cachep);
out_free_dbc_cachep:
	kmem_cache_destroy(lightfs_dbc_cachep);
out_free_meta_buf_cachep:
	kmem_cache_destroy(lightfs_meta_buf_cachep);
out_free_buf_cachep:
	kmem_cache_destroy(lightfs_buf_cachep);
out_free_txn_buf_cachep:
	kmem_cache_destroy(lightfs_txn_buf_cachep);
out_free_txn_cachep:
	kmem_cache_destroy(lightfs_txn_cachep);
out_free_c_txn_cachep:
	kmem_cache_destroy(lightfs_c_txn_cachep);
	return ret;
}

int lightfs_txn_hdlr_destroy(void)
{
	int i;
	for (i = 0; i < CONCURRENT_CNT; i++) {
		flush_workqueue(txn_hdlr->workqs[i]);
		destroy_workqueue(txn_hdlr->workqs[i]);
	}
	kfree(txn_hdlr->workqs);
	lightfs_queue_exit(txn_hdlr->workq_tags);
	kthread_stop(txn_hdlr->tsk);
	flush_workqueue(txn_hdlr->commit_workq);
	destroy_workqueue(txn_hdlr->commit_workq);
	txn_hdlr->db_io->close(txn_hdlr->db_io);
	kmem_cache_destroy(lightfs_dbc_buf_cachep);
	kmem_cache_destroy(lightfs_dbc_cachep);
	kmem_cache_destroy(lightfs_buf_cachep);
	kmem_cache_destroy(lightfs_meta_buf_cachep);
	kmem_cache_destroy(lightfs_txn_buf_cachep);
	kmem_cache_destroy(lightfs_txn_cachep);
	kmem_cache_destroy(lightfs_c_txn_cachep);

	return 0;
}

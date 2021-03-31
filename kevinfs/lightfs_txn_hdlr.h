#ifndef __TXN_HDLR__
#define __TXN_HDLR__

#include <linux/list.h>
#include <linux/spinlock.h> 
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include "bloomfilter.h"
#include "db.h"
#include "lightfs.h"

static inline void txn_hdlr_alloc(struct __lightfs_txn_hdlr **__txn_hdlr)
{
	struct __lightfs_txn_hdlr *_txn_hdlr = (struct __lightfs_txn_hdlr *)kzalloc(sizeof(struct __lightfs_txn_hdlr), GFP_NOIO);

	_txn_hdlr->txn_id = 1;
	_txn_hdlr->running_c_txn_id = 0;
	_txn_hdlr->txn_cnt = 0;
	_txn_hdlr->ordered_c_txn_cnt = 0;
	_txn_hdlr->orderless_c_txn_cnt = 0;
	_txn_hdlr->committing_c_txn_cnt = 0;
	_txn_hdlr->running_c_txn_cnt = 0;
	init_waitqueue_head(&_txn_hdlr->wq);
	init_waitqueue_head(&_txn_hdlr->txn_wq);
	INIT_LIST_HEAD(&_txn_hdlr->txn_list);
	INIT_LIST_HEAD(&_txn_hdlr->sync_txn_list);
	INIT_LIST_HEAD(&_txn_hdlr->ordered_c_txn_list);
	INIT_LIST_HEAD(&_txn_hdlr->orderless_c_txn_list);
	INIT_LIST_HEAD(&_txn_hdlr->committed_c_txn_list);
	spin_lock_init(&_txn_hdlr->txn_hdlr_spin);
	spin_lock_init(&_txn_hdlr->txn_spin);
	spin_lock_init(&_txn_hdlr->ordered_c_txn_spin);
	spin_lock_init(&_txn_hdlr->orderless_c_txn_spin);
	spin_lock_init(&_txn_hdlr->c_txn_spin);
	spin_lock_init(&_txn_hdlr->running_c_txn_spin);
	_txn_hdlr->state = false;
	_txn_hdlr->syncing_cnt = 0;
	_txn_hdlr->contention = false;
	_txn_hdlr->running_c_txn = NULL;
	_txn_hdlr->sync_c_txn = NULL;
	_txn_hdlr->txn_buffer = RB_ROOT;
	init_rwsem(&_txn_hdlr->txn_buffer_sem);
	spin_lock_init(&_txn_hdlr->txn_buffer_spin);
	_txn_hdlr->current_workq_id = 0;
	*__txn_hdlr = _txn_hdlr;
	_txn_hdlr->running = 1;
	
}

static inline void c_txn_list_alloc(DB_C_TXN_LIST **c_txn_list, DB_C_TXN *c_txn)
{
	*c_txn_list = kmalloc(sizeof(DB_C_TXN_LIST), GFP_NOIO);
	(*c_txn_list)->c_txn_ptr = c_txn;
	INIT_LIST_HEAD(&((*c_txn_list)->c_txn_list));
}

static inline void c_txn_list_free(DB_C_TXN_LIST *c_txn_list)
{
	kfree(c_txn_list);
}

static inline int calc_txn_buf_size(DB_TXN_BUF *txn_buf)
{
	int total = 0;
	total += sizeof(uint8_t) + sizeof(uint16_t) + txn_buf->key_len + sizeof(uint16_t) + sizeof(uint16_t) + txn_buf->len;
	// total = type, key_len, key, val_off, val_len, val
	
	return total;
}

static inline int c_txn_is_available(DB_C_TXN *c_txn, DB_TXN *txn)
{
	return c_txn->size + txn->size > C_TXN_LIMIT_BYTES ? 0 : 1;
}

static inline int c_txn_available_bytes(DB_C_TXN *c_txn)
{
	return C_TXN_LIMIT_BYTES - c_txn->size;
}

static inline int diff_c_txn_and_txn(DB_C_TXN *c_txn, DB_TXN *txn)
{
	return C_TXN_LIMIT_BYTES - c_txn->size - txn->size;
}

static inline void txn_buf_setup(DB_TXN_BUF *txn_buf, const void *data, uint32_t off, uint32_t size, enum lightfs_req_type type)
{
	txn_buf->off = off;
	txn_buf->len = size;
	txn_buf->buf = (char*)data;
	txn_buf->type = type;
}

static inline void txn_buf_setup_cpy(DB_TXN_BUF *txn_buf, const void *data, uint32_t off, uint32_t size, enum lightfs_req_type type)
{
	char *data_buf = (char *)data;
	txn_buf->off = off;
	txn_buf->len = size;
	memcpy(txn_buf->buf+off, data_buf, size); // TMP
	txn_buf->type = type;
}

static inline void alloc_txn_buf_key_from_dbt(DB_TXN_BUF *txn_buf, const DBT *dbt)
{
	txn_buf->key = kmalloc(dbt->size, GFP_KERNEL);
	memcpy(txn_buf->key, dbt->data, dbt->size);
	txn_buf->key_len = dbt->size;
}

static inline void copy_txn_buf_key_from_dbt(DB_TXN_BUF *txn_buf, DBT *dbt)
{
	//memcpy(txn_buf->key, dbt->data, dbt->size);
	txn_buf->key = dbt->data;
	txn_buf->key_len = dbt->size;
}



static inline uint32_t copy_dbt_from_dbc(DBC *dbc, DBT *dbt)
{
	dbt->size = *((uint16_t *)(dbc->buf + dbc->idx));
	if (dbt->size) {
		memcpy(dbt->data, dbc->buf + dbc->idx + sizeof(uint16_t), dbt->size);
	}
	
	return sizeof(uint16_t) + dbt->size;
}

static inline uint32_t copy_value_dbt_from_dbc(DBC *dbc, DBT *dbt)
{
	uint16_t size = *((uint16_t *)(dbc->buf + dbc->idx));
	if (dbt->size) {
		memcpy(dbt->data, dbc->buf + dbc->idx + sizeof(uint16_t), dbt->size);
	}
	
	return sizeof(uint16_t) + size;
}

static inline uint16_t dbc_get_size(DBC *dbc)
{
	return *((uint16_t *)(dbc->buf + dbc->idx));
}


//int lightfs_bstore_txn_begin(DB_TXN *, DB_TXN **, uint32_t);
//int lightfs_bstore_txn_commit(DB_TXN *, uint32_t);
//int lightfs_bstore_txn_abort(DB_TXN *);
int lightfs_txn_hdlr_init(void);
int lightfs_txn_hdlr_destroy(void);
int lightfs_bstore_txn_insert(DB *, DB_TXN *, const DBT *, const DBT *, uint32_t, enum lightfs_req_type);
int lightfs_bstore_txn_get(DB *, DB_TXN *, DBT *, DBT *, uint32_t, enum lightfs_req_type);
int lightfs_bstore_txn_get_multi(DB *, DB_TXN *, DBT *, uint32_t, YDB_CALLBACK_FUNCTION, void *, enum lightfs_req_type);
int lightfs_bstore_txn_get_multi_reada(DB *, DB_TXN *, DBT *, uint32_t, void *, enum lightfs_req_type);
int lightfs_bstore_txn_sync_put(DB *, DB_TXN *, DBT *, DBT *, uint32_t, enum lightfs_req_type);
int lightfs_bstore_dbc_cursor(DB *, DB_TXN *, DBC **, enum lightfs_req_type);


#endif


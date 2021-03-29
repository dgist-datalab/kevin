#ifndef __LIGHTFS_H__
#define __LIGHTFS_H__

#include "lightfs_fs.h"
#include <linux/workqueue.h>

//#define LIGHTFS_TXN_LIMIT 365
#define LIGHTFS_TXN_LIMIT 310
#define UINT16_MAX (65535U)
#define LIGHTFS_IO_LARGE_BUF (2 * 1024 * 1024)
#define LIGHTFS_IO_SMALL_BUF (4 * 1024)

//#define C_TXN_LIMIT_BYTES (2 * 1024 * 1024)
//#define C_TXN_LIMIT_BYTES (1569760) // Org
#define C_TXN_LIMIT_BYTES (1569760)
//#define C_TXN_LIMIT_BYTES (1301892)
//#define C_TXN_LIMIT_BYTES (1301892)
#define C_TXN_BLOOM_M_BYTES 308 // 512 items, p=0.1
//#define C_TXN_BLOOM_M_BYTES 791 // 512 items, p=0.01
#define C_TXN_BLOOM_K 3
//#define C_TXN_COMMITTING_LIMIT 16
#define C_TXN_COMMITTING_LIMIT 70
#define RUNNING_C_TXN_LIMIT 16
//#define TXN_LIMIT 10
#define SOFT_TXN_LIMIT (64 * 1024)
#define HARD_TXN_LIMIT (128 * 1024)
//#define TXN_THRESHOLD 5
#define TXN_THRESHOLD (64 * 1024)
#define DBC_LIMIT 1024
#define ITER_BUF_SIZE LIGHTFS_IO_LARGE_BUF
#define KMEM_CACHE_FLAG (SLAB_RECLAIM_ACCOUNT)
#define TXN_FLUSH_TIME 10
#define TXN_SLEEP_TIME 100
#define INODE_SIZE 152
#define HASHTABLE_BITS 20
#define CONCURRENT_CNT 2
#define OPS_CNT 30
#define MISS_RATE 10
#define READA_BLOCK_CNT 256
#define READA_THRESHOLD 64
#define READA_REISSUE 32
#define READA_MULTIPLIER 1
#define READA_QD 4
#define ENORA 99999
#define ESMALL 9999
#define EEOF 999



typedef struct __lightfs_db_io DB_IO;
typedef struct __lightfs_txn_buffer DB_TXN_BUF;
typedef struct __lightfs_c_txn DB_C_TXN;
typedef struct __lightfs_c_txn_list DB_C_TXN_LIST;
typedef uint32_t TXNID_T;

struct lightfs_queue_item {
    void *data;
    int id; 
    struct list_head tag;
};

struct lightfs_queue {
    struct semaphore slots;
    struct semaphore items;
    struct list_head free_tag_list;
    struct list_head processing_tag_list;
    spinlock_t queue_spin;
    struct lightfs_queue_item **item_arr;
    int cap;
};


struct __lightfs_txn_buffer {
	TXNID_T txn_id;
	struct list_head txn_buf_list;
	char *key;
	uint16_t key_len;
	uint16_t off;
	uint32_t len;
	uint16_t update;
	enum lightfs_req_type type;
	char *buf;
	uint32_t ret;
	DB *db;
	struct rb_node rb_node;
	bool is_rb;
	bool is_deleted;
	DB_TXN *txn;
#ifdef TIME_CHECK
	ktime_t create;
	ktime_t queue;
	ktime_t insert;
	ktime_t transfer;
	ktime_t complete;
	ktime_t free;
#endif
	//void * (*txn_buf_cb)(void *data);
	//struct completion *completionp;
};

struct __lightfs_c_txn {
	TXNID_T txn_id;
	struct list_head c_txn_list;
	struct list_head txn_list;
	struct list_head children;
	uint32_t size;
	uint16_t parents;
	enum lightfs_txn_state state;
	//uint32_t state;
	struct bloomfilter *filter;
	struct work_struct transfer_work;
	struct work_struct commit_work;
	struct work_struct work;
	uint64_t workq_id;
	uint32_t committing_cnt;
	uint32_t cnt;
};

struct __lightfs_c_txn_list {
	DB_C_TXN *c_txn_ptr;
	struct list_head c_txn_list;
};

struct lightfs_monitor {
	atomic64_t ops_num[OPS_CNT];
};

struct __lightfs_db_io {
	int (*get) (DB *db, DB_TXN_BUF *txn_buf);
	int (*sync_put) (DB *db, DB_TXN_BUF *txn_buf);
	int (*iter) (DB *db, DBC *dbc, DB_TXN_BUF *txn_buf);
	int (*transfer) (DB *db, DB_C_TXN *c_txn, void *(*cb)(void *data), void *extra);
	int (*commit) (DB_TXN_BUF *txn_buf);
	int (*close) (DB_IO *db_io);
	int (*get_multi) (DB *db, DB_TXN_BUF *txn_buf);
	int (*get_multi_reada) (DB *db, DB_TXN_BUF *txn_buf, void *extra);
	struct lightfs_monitor mon;
};

struct __lightfs_txn_hdlr {
	struct task_struct *tsk;
	wait_queue_head_t wq;
	wait_queue_head_t txn_wq;
	wait_queue_head_t txn_sync_wq;
	TXNID_T txn_id;
	uint32_t txn_cnt;
	uint32_t ordered_c_txn_cnt;
	uint32_t orderless_c_txn_cnt;
	uint32_t committing_c_txn_cnt;
	uint32_t running_c_txn_cnt;
	uint64_t running_c_txn_id;
	struct list_head txn_list;
	struct list_head sync_txn_list;
	struct list_head ordered_c_txn_list;
	struct list_head orderless_c_txn_list;
	struct list_head committed_c_txn_list;
	bool state;
	uint16_t syncing_cnt;
	bool contention;
	spinlock_t txn_hdlr_spin;
	spinlock_t txn_spin;
	spinlock_t ordered_c_txn_spin;
	spinlock_t orderless_c_txn_spin;
	spinlock_t c_txn_spin;
	spinlock_t running_c_txn_spin;
	spinlock_t sync_txn_spin;
	DB_IO *db_io;
	DB_C_TXN *running_c_txn;
	DB_C_TXN *sync_c_txn;
	struct rb_root txn_buffer;
	struct rw_semaphore txn_buffer_sem;
	spinlock_t txn_buffer_spin;
	struct workqueue_struct *commit_workq;
	struct workqueue_struct **workqs;
	struct lightfs_queue *workq_tags;
	uint64_t current_workq_id;
};

#endif

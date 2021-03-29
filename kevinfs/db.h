#ifndef __DB_H__
#define __DB_H__

#include <linux/slab.h>
#include <linux/fs.h>

#ifdef LIGHTFS
typedef struct __lightfs_db_txn DB_TXN;
typedef struct __lightfs_dbt DBT;
typedef struct __lightfs_dbc DBC;
typedef struct __lightfs_db_env DB_ENV;
typedef struct __lightfs_db DB;
#endif

typedef enum {
	DB_BTREE=1,
	DB_UNKNOWN=5
} DBTYPE;

typedef int(*YDB_CALLBACK_FUNCTION)(DBT const*, DBT const*, void*);
#define DB_VERB_DEADLOCK 1
#define DB_VERB_RECOVERY 8
#define DB_VERB_REPLICATION 32
#define DB_VERB_WAITSFOR 64
#define DB_ARCH_ABS 1
#define DB_ARCH_LOG 4
#define DB_CREATE 1
#define DB_CXX_NO_EXCEPTIONS 1
#define DB_EXCL 16384
#define DB_PRIVATE 8388608
#define DB_RDONLY 32
#define DB_RECOVER 64
#define DB_RUNRECOVERY -30975
#define DB_THREAD 128
#define DB_TXN_NOSYNC 512
#define DB_BLACKHOLE 524288
#define DB_LOCK_DEFAULT 1
#define DB_LOCK_OLDEST 7
#define DB_LOCK_RANDOM 8
#define DB_KEYFIRST 13
#define DB_KEYLAST 14
#define DB_NOOVERWRITE 20
#define DB_NODUPDATA 19
#define DB_NOOVERWRITE_NO_ERROR 1
#define DB_OPFLAGS_MASK 255
#define DB_AUTO_COMMIT 33554432
#define DB_INIT_LOCK 131072
#define DB_INIT_LOG 262144
#define DB_INIT_MPOOL 524288
#define DB_INIT_TXN 2097152
#define DB_KEYEXIST -30996
#define DB_LOCK_DEADLOCK -30995
#define DB_LOCK_NOTGRANTED -30994
#define DB_NOTFOUND -30989
#define DB_NOTFOUND_DCACHE -30985
#define DB_NOTFOUND_DCACHE_FULL -30984
#define DB_FOUND_FREE -30987
#define DB_SECONDARY_BAD -30974
#define DB_DONOTINDEX -30998
#define DB_BUFFER_SMALL -30999
#define DB_BADFORMAT -30500
#define DB_DELETE_ANY 65536
#define DB_FIRST 7
#define DB_LAST 15
#define DB_CURRENT 6
#define DB_NEXT 16
#define DB_PREV 23
#define DB_SET 26
#define DB_SET_RANGE 27
#define DB_CURRENT_BINDING 253
#define DB_SET_RANGE_REVERSE 252
#define DB_RMW 1073741824
#define DB_IS_RESETTING_OP 0x01000000
#define DB_PRELOCKED 0x00800000
#define DB_PRELOCKED_WRITE 0x00400000
#define DB_IS_HOT_INDEX 0x00100000
#define DBC_DISABLE_PREFETCHING 0x20000000
#define DB_UPDATE_CMP_DESCRIPTOR 0x40000000
#define DB_DBT_APPMALLOC 1
#define DB_DBT_DUPOK 2
#define DB_DBT_MALLOC 8
#define DB_DBT_MULTIPLE 16
#define DB_DBT_REALLOC 64
#define DB_DBT_USERMEM 256
#define DB_LOG_AUTOREMOVE 524288
#define DB_TXN_WRITE_NOSYNC 4096
#define DB_TXN_NOWAIT 1024
#define DB_TXN_SYNC 16384
#define DB_TXN_SNAPSHOT 268435456
#define DB_READ_UNCOMMITTED 134217728
#define DB_READ_COMMITTED 67108864
#define DB_INHERIT_ISOLATION 1
#define DB_SERIALIZABLE 2
#define DB_TXN_READ_ONLY 4


#ifdef LIGHTFS

#include <linux/list.h>

enum lightfs_txn_state {
	TXN_CREATED = 0,
	TXN_COMMITTING = 1,
	TXN_COMMITTED = 2,
	TXN_ABORTED = 4,
	TXN_INSERTING = 8,
	TXN_TRANSFERING = 16,
	TXN_FLUSH = 32,
	TXN_READ = 64,
	TXN_SYNC = 128,
	TXN_ORDERED = 256,
	TXN_ORDERLESS = 512,
};

enum lightfs_req_type {
	LIGHTFS_META_GET = 0,
	LIGHTFS_META_SET,
	LIGHTFS_META_SYNC_SET,
	LIGHTFS_META_DEL,
	LIGHTFS_META_CURSOR,
	LIGHTFS_META_UPDATE,
	LIGHTFS_META_RENAME,
	LIGHTFS_DATA_GET,
	LIGHTFS_DATA_SET,
	LIGHTFS_DATA_SEQ_SET,
	LIGHTFS_DATA_DEL,
	LIGHTFS_DATA_DEL_MULTI,
	LIGHTFS_DATA_CURSOR,
	LIGHTFS_DATA_UPDATE,
	LIGHTFS_DATA_RENAME,
	LIGHTFS_COMMIT,
	LIGHTFS_GET_MULTI,
	LIGHTFS_DATA_SET_WB,
	LIGHTFS_GET_MULTI_REAL,
	LIGHTFS_DEL_MULTI_REAL,
	LIGHTFS_TXN_TRANSFER,
	LIGHTFS_GET_MULTI_READA,
	LIGHTFS_GET_MULTI_READA_REAL,
};

struct lightfs_db_key_operations {
    int (*keycmp) (DB *, const DBT *, const DBT *);
};

struct __lightfs_db_env {
	struct __lightfs_db_env_internal *i;
#define db_env_struct_i(x) ((x)->i)
	int (*checkpointing_set_period)             (DB_ENV*, uint32_t) /* Change the delay between automatic checkpoints.  0 means disabled. */;
	int (*checkpointing_get_period)             (DB_ENV*, uint32_t*) /* Retrieve the delay between automatic checkpoints.  0 means disabled. */;
	int (*cleaner_set_period)                   (DB_ENV*, uint32_t) /* Change the delay between automatic cleaner attempts.  0 means disabled. */;
	int (*cleaner_get_period)                   (DB_ENV*, uint32_t*) /* Retrieve the delay between automatic cleaner attempts.  0 means disabled. */;
	int (*checkpointing_end_atomic_operation)   (DB_ENV*) /* End   a set of operations (that must be atomic as far as checkpoints are concerned). */;
	int (*get_engine_status_text)               (DB_ENV*, char*, int)     /* Fill in status text */;
	void (*set_update)                          (DB_ENV *env, int (*update_function)(DB *, const DBT *key, const DBT *old_val, const DBT *extra, void (*set_val)(const DBT *new_val, void *set_extra), void *set_extra));

  	int (*set_key_ops)                          (DB_ENV *env, struct lightfs_db_key_operations *key_ops);
	void (*change_fsync_log_period)             (DB_ENV*, uint32_t);
	int  (*close) (DB_ENV *, uint32_t);
	void (*err) (const DB_ENV *, int, const char *, ...) __attribute__ (( format (printf, 3, 4) ));
	int  (*get_cachesize) (DB_ENV *, uint32_t *, uint32_t *, int *);
	int  (*open) (DB_ENV *, const char *, uint32_t, int);
	int  (*set_cachesize) (DB_ENV *, uint32_t, uint32_t, int);
	int  (*set_flags) (DB_ENV *, uint32_t, int);
	int  (*txn_begin) (DB_ENV *, DB_TXN *, DB_TXN **, uint32_t);
};

struct __lightfs_db {
	struct __lightfs_db_internal *i;
	int (*change_descriptor) (DB*, DB_TXN*, const DBT* descriptor, uint32_t) /* change row/dictionary descriptor for a db.  Available only while db is open */;
	int (*getf_set)(DB*, DB_TXN*, uint32_t, DBT*, YDB_CALLBACK_FUNCTION, void*) /* same as DBC->c_getf_set without a persistent cursor) */;
	int (*update)(DB *, DB_TXN*, const DBT *key, const DBT *extra, loff_t offset, enum lightfs_req_type);
	int (*seq_put)(DB *, DB_TXN *, DBT *, DBT *, enum lightfs_req_type);
	DB_ENV *dbenv;
	void *api_internal;
	int (*close) (DB*, uint32_t);
	int (*cursor) (DB *, DB_TXN *, DBC **, enum lightfs_req_type);
	int (*del) (DB *, DB_TXN *, DBT *, enum lightfs_req_type);
	int (*weak_del) (DB *, DB_TXN *, DBT *, enum lightfs_req_type);
#ifdef PINK
	int (*del_multi) (DB *, DB_TXN *, DBT *, uint32_t , bool, enum lightfs_req_type);
#else
	int (*del_multi) (DB *, DB_TXN *, DBT *, DBT *, bool, enum lightfs_req_type);
#endif
#ifdef GET_MULTI
	int (*get_multi) (DB *, DB_TXN *, DBT *, uint32_t, YDB_CALLBACK_FUNCTION, void *, enum lightfs_req_type);
#endif
	int (*get_multi_reada) (DB *, DB_TXN *, DBT *, uint32_t, void *, enum lightfs_req_type);
	int (*get) (DB *, DB_TXN *, DBT *, DBT *, enum lightfs_req_type);
	int (*get_flags) (DB *, uint32_t *);
	int (*open) (DB *, DB_TXN *, const char *, const char *, DBTYPE, uint32_t, int);
	int (*put) (DB *, DB_TXN *, DBT *, DBT *, enum lightfs_req_type);
	int (*sync_put) (DB *, DB_TXN *, DBT *, DBT *, enum lightfs_req_type);
	int (*rename) (DB *, DB_TXN *, DBT *, DBT *, DBT *, DBT *, enum lightfs_req_type);
	int (*cache_get) (DB *, DB_TXN *, DBT *, DBT *, enum lightfs_req_type);
	int (*cache_weak_del) (DB *, DB_TXN *, DBT *, enum lightfs_req_type);
	int (*cache_del) (DB *, DB_TXN *, DBT *, enum lightfs_req_type, bool is_dir);
	int (*cache_put) (DB *, DB_TXN *, DBT *, DBT *, enum lightfs_req_type, struct inode *dir_inode, bool is_dir);
};


enum lightfs_c_txn_state {
	C_TXN_ORDERED = 0,
	C_TXN_ORDERLESS,
};

struct __lightfs_db_txn {
	uint32_t txn_id;
	uint32_t cnt;
	uint32_t size;
	enum lightfs_txn_state state;
	struct list_head txn_list;
	struct list_head txn_buf_list;
	struct completion *completionp;
#ifdef TXN_TIME_CHECK
	bool is_inserted;
	ktime_t create;
	ktime_t begin;
	ktime_t wakeup;
	ktime_t first_insert;
	ktime_t last_insert;
	ktime_t commit;
	ktime_t free;
#endif
};

struct __lightfs_dbt {
	void*data;
	uint32_t size;
	uint32_t ulen;
	uint32_t flags;
};

struct __lightfs_dbc {
	int (*c_getf_first)(DBC *, uint32_t, YDB_CALLBACK_FUNCTION, void *);
	int (*c_getf_last)(DBC *, uint32_t, YDB_CALLBACK_FUNCTION, void *);
	int (*c_getf_next)(DBC *, uint32_t, YDB_CALLBACK_FUNCTION, void *);
	int (*c_getf_prev)(DBC *, uint32_t, YDB_CALLBACK_FUNCTION, void *);
	int (*c_getf_current)(DBC *, uint32_t, YDB_CALLBACK_FUNCTION, void *);
	int (*c_getf_set)(DBC *, uint32_t, DBT *, YDB_CALLBACK_FUNCTION, void *);
	int (*c_getf_set_range)(DBC *, uint32_t, DBT *, YDB_CALLBACK_FUNCTION, void *);
	int (*c_getf_set_range_reverse)(DBC *, uint32_t, DBT *, YDB_CALLBACK_FUNCTION, void *);
	int (*c_set_bounds)(DBC*, const DBT*, const DBT*, bool pre_acquire, int out_of_range_error);
	void (*c_set_check_interrupt_callback)(DBC*, bool (*)(void*), void *);
	void (*c_remove_restriction)(DBC*);
	DB *dbp;
	int (*c_close) (DBC *);
	int (*c_get) (DBC *, DBT *, DBT *, uint32_t);
	int idx;
	int old_idx;
	int buf_len;
	char *buf;
	DBT key;
	DBT value;
	void *extra;
	int io_tag;
#ifdef CHEEZE
	struct __lightfs_dbc *cheeze_dbc;
#endif
};

#endif

#endif

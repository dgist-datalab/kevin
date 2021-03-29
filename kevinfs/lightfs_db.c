#include "db.h"
#include "lightfs_fs.h"
#include "lightfs.h"
#include "lightfs_txn_hdlr.h"
#include "rbtreekv.h"

int lightfs_db_open (DB *db, DB_TXN *txn, const char *file, const char *database, DBTYPE type, uint32_t flag, int mode)
{
	return 0;
}

int lightfs_db_close (DB* db, uint32_t flag)
{
#if (defined EMULATION || defined CHEEZE)
	db_close(db, flag);
#endif
	kfree(db);

	return 0;
}

int lightfs_db_get (DB *db, DB_TXN *txn, DBT *key, DBT *value, enum lightfs_req_type type) {
	return lightfs_bstore_txn_get(db, txn, key, value, 0, type);
}

int lightfs_db_put (DB *db, DB_TXN *txn, DBT *key, DBT *value, enum lightfs_req_type type)
{
	return lightfs_bstore_txn_insert(db, txn, key, value, 0, type);
}

int lightfs_db_sync_put (DB *db, DB_TXN *txn, DBT *key, DBT *value, enum lightfs_req_type type)
{
	return lightfs_bstore_txn_sync_put(db, txn, key, value, 0, type);
}


int lightfs_db_seq_put(DB *db, DB_TXN *txn, DBT *key, DBT *value, enum lightfs_req_type type)
{
	return lightfs_bstore_txn_insert(db, txn, key, value, 0, type);
}


int lightfs_db_update(DB *db, DB_TXN *txn, const DBT *key, const DBT *value, loff_t offset, enum lightfs_req_type type)
{
	return lightfs_bstore_txn_insert(db, txn, key, value, offset, type);
}

int lightfs_db_del (DB *db , DB_TXN *txn, DBT *key, enum lightfs_req_type type)
{
	return lightfs_bstore_txn_insert(db, txn, key, NULL, 1, type);
}

#ifdef PINK
int lightfs_db_del_multi (DB *db, DB_TXN *txn, DBT *min_key, uint32_t key_cnt, bool a, enum lightfs_req_type type)
{
	//TODO:: fix del_multi
	if (min_key == 0)
		return 0;
	else
		return lightfs_bstore_txn_insert(db, txn, min_key, NULL, key_cnt, type);
}
#else 
int lightfs_db_del_multi (DB *db, DB_TXN *txn, DBT *min_key, DBT *max_key, bool a, enum lightfs_req_type type)
{
	//TODO:: fix del_multi
	return lightfs_bstore_txn_insert(db, txn, min_key, NULL, 0, type);
}
#endif

#ifdef GET_MULTI
int lightfs_db_get_multi (DB *db, DB_TXN *txn, DBT *key, uint32_t cnt, YDB_CALLBACK_FUNCTION f, void *extra, enum lightfs_req_type type)
{
	return lightfs_bstore_txn_get_multi(db, txn, key, cnt, f, extra, type);
}
#endif

int lightfs_db_get_multi_reada (DB *db, DB_TXN *txn, DBT *key, uint32_t cnt, void *extra, enum lightfs_req_type type)
{
	return lightfs_bstore_txn_get_multi_reada(db, txn, key, cnt, extra, type);
}


int lightfs_db_cursor (DB *db, DB_TXN *txn, DBC **dbc, enum lightfs_req_type type)
{
#ifdef EMULATION
	return db_cursor(db, txn, dbc, 0);
#else
	return lightfs_bstore_dbc_cursor(db, txn, dbc, type);
#endif
}

int lightfs_db_change_descriptor (DB *db, DB_TXN *txn, const DBT *descriptor, uint32_t a)
{
	return 0;
}

int lightfs_db_create(DB **db, DB_ENV *env, uint32_t flags)
{
	*db = kmalloc(sizeof(DB), GFP_NOIO);
	if (*db == NULL) {
		return -ENOMEM;
	}
#if (defined EMULATION || defined CHEEZE)
	db_create(db, env, flags);
#endif
	BUG_ON((*db)->i == NULL);
	(*db)->dbenv = env;

	(*db)->open = lightfs_db_open;
	(*db)->close = lightfs_db_close;
	(*db)->get = lightfs_db_get;
	(*db)->put = lightfs_db_put;
	(*db)->sync_put = lightfs_db_sync_put;
	(*db)->seq_put = lightfs_db_seq_put;
	(*db)->update = lightfs_db_update;
	(*db)->del = lightfs_db_del;
	(*db)->del_multi = lightfs_db_del_multi;
	(*db)->cursor = lightfs_db_cursor;
	(*db)->change_descriptor = lightfs_db_change_descriptor;
	(*db)->get_multi_reada = lightfs_db_get_multi_reada;
#ifdef GET_MULTI
	(*db)->get_multi = lightfs_db_get_multi;
#endif

	return 0;
}

#ifndef __RBTREEKV_H__
#define __RBTREEKV_H__

int db_env_set_default_bt_compare(DB_ENV *env, int (*bt_compare)(DB *, const DBT *, const DBT *));
int db_cursor(DB *db, DB_TXN *txnid, DBC **cursorp, uint32_t flags);
int db_env_create(DB_ENV **envp, uint32_t flags);
int db_create(DB **db, DB_ENV *env, uint32_t flags);
int db_env_close(DB_ENV *env, uint32_t flag);
int db_get(DB *db, DB_TXN *txnid, DBT *key, DBT *data, uint32_t flags);
int db_del(DB *db, DB_TXN *txnid, DBT *key, uint32_t flags);
int db_put(DB *db, DB_TXN *txnid, DBT *key, DBT *data, uint32_t flags);
int db_close(DB *db, uint32_t flag);
int db_update(DB *db, DB_TXN *txnid, const DBT *key, const DBT *value, loff_t offset, uint32_t flags);


int db_cache_del(DB *db, DB_TXN *txnid, DBT *key, uint32_t flags);
int db_cache_weak_del(DB *db, DB_TXN *txnid, DBT *key, uint32_t flags);
int db_cache_get(DB *db, DB_TXN *txnid, DBT *key, DBT *value, uint32_t flags);
int db_cache_put(DB *db, DB_TXN *txnid, DBT *key, DBT *value, uint32_t flags);
int db_cache_close(DB *db, uint32_t flag);
int db_cache_create(DB **db, DB_ENV *env, uint32_t flags);

#endif

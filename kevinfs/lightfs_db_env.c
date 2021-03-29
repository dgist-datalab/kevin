#include "db.h"
#include "lightfs_fs.h"
#include "lightfs_txn_hdlr.h"
#include "rbtreekv.h"

int lightfs_db_env_set_cachesize (DB_ENV *env, uint32_t a, uint32_t b, int c)
{
	return 0;
}

int lightfs_db_env_set_key_ops (DB_ENV *env, struct lightfs_db_key_operations *key_ops)
{
	db_env_set_default_bt_compare(env, key_ops->keycmp); 
	return 0;
}

void lightfs_db_env_set_update (DB_ENV *env, int (*update_function)(DB *, const DBT *key, const DBT *old_val, const DBT *extra, void (*set_val)(const DBT *new_val, void *set_extra), void *set_extra))
{
	return;
}

int lightfs_db_env_open (DB_ENV *env, const char *a, uint32_t b, int c)
{
	return 0;
}

int lightfs_db_env_close (DB_ENV *env, uint32_t a)
{
	lightfs_txn_hdlr_destroy();
	db_env_close(env, a);
	kfree(env);

	return 0;
}


int lightfs_db_env_create(DB_ENV **envp, uint32_t flags)
{
	*envp = kmalloc(sizeof(DB_ENV), GFP_NOIO);
	if (*envp == NULL) {
		return -ENOMEM;
	}
	db_env_create(envp, flags);
	(*envp)->set_cachesize = lightfs_db_env_set_cachesize;
	(*envp)->set_key_ops = lightfs_db_env_set_key_ops;
	(*envp)->set_update = lightfs_db_env_set_update;
	(*envp)->open = lightfs_db_env_open;
	(*envp)->close = lightfs_db_env_close;

	lightfs_txn_hdlr_init();

	return 0;
}

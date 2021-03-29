#ifndef __LIGHTFS_DB_ENV_H__
#define __LIGHTFS_DB_ENV_H__

#include "lightfs_fs.h"

int lightfs_db_env_create(DB_ENV **envp, uint32_t flags);

#endif

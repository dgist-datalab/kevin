#ifndef __LIGHTFS_DB_H__
#define __LIGHTFS_DB_H__

#include "lightfs_fs.h"

int lightfs_db_create(DB **db, DB_ENV *env, uint32_t flags);

#endif

#ifndef __H_MURMUR3_H__
#define __H_MURMUR3_H__

#include "db.h"

void
murmur3_hash32(const void *key, size_t len, uint32_t seed, void *out);


#endif /* __H_MURMUR3_H__ */

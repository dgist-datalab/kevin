#ifndef __BLOOMFILTER_H__
#define __BLOOMFILTER_H__

#include "lightfs_fs.h"

struct bloomfilter {
	unsigned int  m;
	unsigned int  k;
	unsigned char bit_vector[1];
};

void
bloomfilter_init(struct bloomfilter *bloomfilter, unsigned int m, unsigned int k);

void
bloomfilter_re_init(struct bloomfilter *bloomfilter, unsigned int m, unsigned int k);

void
bloomfilter_set(struct bloomfilter *bloomfilter, const void *key, size_t len);

int
bloomfilter_get(struct bloomfilter *bloomfilter, const void *key, size_t len);


#endif /* __BLOOMFILTER_H__ */

#include "bloomfilter.h"
#include "murmur3.h"

#define bit_set(v,n)    ((v)[(n) >> 3] |= (0x1 << (0x7 - ((n) & 0x7))))
#define bit_get(v,n)    ((v)[(n) >> 3] &  (0x1 << (0x7 - ((n) & 0x7))))
#define bit_clr(v,n)    ((v)[(n) >> 3] &=~(0x1 << (0x7 - ((n) & 0x7))))

void
bloomfilter_init(struct bloomfilter *bloomfilter, unsigned int m, unsigned int k)
{
	memset(bloomfilter, 0, sizeof(*bloomfilter));
	bloomfilter->m = m;
	bloomfilter->k = k;
	memset(bloomfilter->bit_vector, 0, bloomfilter->m >> 3);
}

void
bloomfilter_set(struct bloomfilter *bloomfilter, const void *key, size_t len)
{
	uint32_t i;
	uint32_t h;

	for (i = 0; i < bloomfilter->k; i++) {
		murmur3_hash32(key, len, i, &h);
		h %= bloomfilter->m;
		bit_set(bloomfilter->bit_vector, h);
	}
}

int
bloomfilter_get(struct bloomfilter *bloomfilter, const void *key, size_t len)
{
	uint32_t i;
	uint32_t h;

	for (i = 0; i < bloomfilter->k; i++) {
		murmur3_hash32(key, len, i, &h);
		h %= bloomfilter->m;
		if (!bit_get(bloomfilter->bit_vector, h))
			return 0;
	}
	return 1;
}

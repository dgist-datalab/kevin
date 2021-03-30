// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Park Ju Hyung
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "crc32c.c"

#define PHYS_ADDR 0x3ec0000000
#define CHEEZE_QUEUE_SIZE 1024
#define CHEEZE_BUF_SIZE (2ULL * 1024 * 1024)
#define ITEMS_PER_HP ((1ULL * 1024 * 1024 * 1024) / CHEEZE_BUF_SIZE)
#define BITS_PER_EVENT (sizeof(uint64_t) * 8)

#define EVENT_BYTES (CHEEZE_QUEUE_SIZE / BITS_PER_EVENT)

#define SEND_OFF 0
#define SEND_SIZE (CHEEZE_QUEUE_SIZE * sizeof(uint8_t))

#define RECV_OFF (SEND_OFF + SEND_SIZE)
#define RECV_SIZE (CHEEZE_QUEUE_SIZE * sizeof(uint8_t))

#define SEQ_OFF (RECV_OFF + RECV_SIZE)
#define SEQ_SIZE (CHEEZE_QUEUE_SIZE * sizeof(uint64_t))

#define REQS_OFF (SEQ_OFF + SEQ_SIZE)
#define REQS_SIZE (CHEEZE_QUEUE_SIZE * sizeof(struct cheeze_req))

#define barrier() __asm__ __volatile__("": : :"memory")

#define ureq_print(u) \
	do { \
		printf("%s:%d\n    id=%d\n    op=%d\n    pos=%u\n    len=%u\n", __func__, __LINE__, u->id, u->op, u->pos, u->len); \
	} while (0);

static void *page_addr;
//static void *meta_addr; // page_addr[0] ==> send_event_addr, recv_event_addr, seq_addr, ureq_addr
static uint8_t *send_event_addr; // CHEEZE_QUEUE_SIZE ==> 16B
static uint8_t *recv_event_addr; // 16B
static uint64_t *seq_addr; // 8KB
struct cheeze_req_user *ureq_addr; // sizeof(req) * 1024
static char *data_addr[2]; // page_addr[1]: 1GB, page_addr[2]: 1GB
static uint64_t seq = 0; 

enum req_opf {
	/* read sectors from the device */
	REQ_OP_READ		= 0,
	/* write sectors to the device */
	REQ_OP_WRITE		= 1,
	/* flush the volatile write cache */
	REQ_OP_FLUSH		= 2,
	/* discard sectors */
	REQ_OP_DISCARD		= 3,
	/* get zone information */
	REQ_OP_ZONE_REPORT	= 4,
	/* securely erase sectors */
	REQ_OP_SECURE_ERASE	= 5,
	/* seset a zone write pointer */
	REQ_OP_ZONE_RESET	= 6,
	/* write the same sector many times */
	REQ_OP_WRITE_SAME	= 7,
	/* write the zero filled sector many times */
	REQ_OP_WRITE_ZEROES	= 9,

	/* SCSI passthrough using struct scsi_request */
	REQ_OP_SCSI_IN		= 32,
	REQ_OP_SCSI_OUT		= 33,
	/* Driver private requests */
	REQ_OP_DRV_IN		= 34,
	REQ_OP_DRV_OUT		= 35,

	REQ_OP_LAST,
};

struct cheeze_req_user {
	int id;
	int op;
	unsigned int pos; // sector_t
	unsigned int len;
} __attribute__((aligned(8), packed));

#define COPY_TARGET "/dev/hugepages/disk"
#define TRACE_TARGET "/trace"

static inline char *get_buf_addr(char **pdata_addr, int id) {
	int idx = id / ITEMS_PER_HP;
	return pdata_addr[idx] + ((id % ITEMS_PER_HP) * CHEEZE_BUF_SIZE);
}

static void shm_meta_init(void *ppage_addr) {
	//memset(ppage_addr, 0, (1ULL * 1024 * 1024 * 1024));
	send_event_addr = ppage_addr + SEND_OFF; // CHEEZE_QUEUE_SIZE ==> 16B
	recv_event_addr = ppage_addr + RECV_OFF; // 16B
	seq_addr = ppage_addr + SEQ_OFF; // 8KB
	ureq_addr = ppage_addr + REQS_OFF; // sizeof(req) * 1024
}

#if 0
// Warning, output is static so this function is not reentrant
static const char *humanSize(uint64_t bytes)
{
	static char output[200];

	char *suffix[] = { "B", "KiB", "MiB", "GiB", "TiB" };
	char length = sizeof(suffix) / sizeof(suffix[0]);

	int i = 0;
	double dblBytes = bytes;

	if (bytes > 1024) {
		for (i = 0; (bytes / 1024) > 0 && i < length - 1;
		     i++, bytes /= 1024)
			dblBytes = bytes / 1024.0;
	}

	sprintf(output, "%.02lf %s", dblBytes, suffix[i]);

	return output;
}
#endif

static off_t fdlength(int fd)
{
	struct stat st;
	off_t cur, ret;

	if (!fstat(fd, &st) && S_ISREG(st.st_mode))
		return st.st_size;

	cur = lseek(fd, 0, SEEK_CUR);
	ret = lseek(fd, 0, SEEK_END);
	lseek(fd, cur, SEEK_SET);

	return ret;
}

static inline uint64_t ts_to_ns(struct timespec* ts) {
	return ts->tv_sec * (uint64_t)1000000000L + ts->tv_nsec;
}

#define TOTAL_SIZE (3ULL * 1024L * 1024L * 1024L) // 3 GB

static void mem_init()
{
	uint64_t pagesize, addr, len;
	int fd;

	fd = open("/dev/mem", O_RDWR);
	if (fd == -1) {
		perror("Failed to open /dev/mem");
		exit(1);
	}

	pagesize = getpagesize();
	addr = PHYS_ADDR & (~(pagesize - 1));
	len = (PHYS_ADDR & (pagesize - 1)) + TOTAL_SIZE;
	page_addr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr);
	if (page_addr == MAP_FAILED) {
		perror("Failed to mmap plain device path");
		exit(1);
	}

	close(fd);
}

int main() {
	int copyfd, dumpfd;
	char *mem;
	uint8_t *send, *recv;
	int i, id;
	unsigned int j;
	uint32_t crc;
	struct cheeze_req_user *ureq;
	char *buf, *page_buf;

	mem_init();
	shm_meta_init(((char*)page_addr) + (2ULL * 1024 * 1024 * 1024));
	data_addr[0] = ((char *)page_addr) + (1ULL * 1024 * 1024 * 1024);
	data_addr[1] = ((char *)page_addr);

	copyfd = open(COPY_TARGET, O_RDWR);
	if (copyfd < 0) {
		perror("Failed to open " COPY_TARGET);
		return 1;
	}

	mem = mmap(NULL, fdlength(copyfd), PROT_READ | PROT_WRITE, MAP_SHARED, copyfd, 0);
	if (mem == MAP_FAILED) {
		perror("Failed to mmap copy path");
		return 1;
	}

	close(copyfd);

	dumpfd = open(TRACE_TARGET, O_WRONLY | O_TRUNC | O_CREAT, 0644);
	if (dumpfd < 0) {
		perror("Failed to open " TRACE_TARGET);
		return 1;
	}

	while (1) {
		for (i = 0; i < CHEEZE_QUEUE_SIZE; i++) {
			send = &send_event_addr[i];
			recv = &recv_event_addr[i];
			if (*send) {
				id = i;
				// printf("id: %d, seq_addr[id]: %lu, seq: %lu\n", id, seq_addr[id], seq);
				ureq = ureq_addr + id;
				// ureq_print(ureq);

				// if (seq_addr[id] == seq) {
					buf = mem + (ureq->pos * 4096ULL);
					page_buf = get_buf_addr(data_addr, id);
					switch (ureq->op) {
						case REQ_OP_READ:
							write(dumpfd, ureq, sizeof(*ureq));
							for (j = 0; j < ureq->len; j += 4096) {
								crc = crc32c(0, buf + j, 4096);
								write(dumpfd, &crc, sizeof(crc));
							}
							memcpy(page_buf, buf, ureq->len);
							break;
						case REQ_OP_WRITE:
							memcpy(buf, page_buf, ureq->len);
							write(dumpfd, ureq, sizeof(*ureq));
							for (j = 0; j < ureq->len; j += 4096) {
								crc = crc32c(0, buf + j, 4096);
								write(dumpfd, &crc, sizeof(crc));
							}
							break;
						case REQ_OP_DISCARD:
							memset(buf, 0, ureq->len);
							write(dumpfd, ureq, sizeof(*ureq));
							for (j = 0; j < ureq->len; j += 4096) {
								crc = 0;
								write(dumpfd, &crc, sizeof(crc));
							}
							break;
					}
					seq++;
					barrier();
					*send = 0;
					barrier();
					*recv = 1;
				// } else {
				// 	continue;
				// }
			}
		}
	}

	close(dumpfd);

	return 0;
}

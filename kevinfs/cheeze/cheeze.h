// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Park Ju Hyung
 */

#ifndef __CHEEZE_H
#define __CHEEZE_H

#define CHEEZE_QUEUE_SIZE 1024
#define CHEEZE_BUF_SIZE (2ULL * 1024 * 1024)
#define HP_SIZE (1024L * 1024L * 1024L)
#define ITEMS_PER_HP (HP_SIZE / CHEEZE_BUF_SIZE)
#define BITS_PER_EVENT (sizeof(uint64_t) * 8)

#define EVENT_BYTES (CHEEZE_QUEUE_SIZE / BITS_PER_EVENT)

#define SEND_OFF 0
#define SEND_SIZE (CHEEZE_QUEUE_SIZE * sizeof(uint8_t))

#define RECV_OFF (SEND_OFF + SEND_SIZE)
#define RECV_SIZE (CHEEZE_QUEUE_SIZE * sizeof(uint8_t))

#define SEQ_OFF (RECV_OFF + RECV_SIZE)
#define SEQ_SIZE (CHEEZE_QUEUE_SIZE * sizeof(uint64_t))

#define REQS_OFF (SEQ_OFF + SEQ_SIZE)
#define REQS_SIZE (CHEEZE_QUEUE_SIZE * sizeof(struct cheeze_req_user))

// #define IS_IN_VM // Change below SHM_ADDRn accordingly!
#ifdef IS_IN_VM
#define SHM_ADDR0 0x800000000
#define SHM_ADDR1 0x840000000
#define SHM_ADDR2 0x880000000
#else
#define SHM_ADDR0 0x3ec0000000
#define SHM_ADDR1 0x3e80000000
#define SHM_ADDR2 0x3e40000000 
#endif

#define SKIP INT_MIN

// #define DEBUG
#define DEBUG_SLEEP 1

#ifdef DEBUG
  #define msleep_dbg msleep
#else
  #define msleep_dbg(...) ((void)0)
#endif

struct cheeze_req_user {
	int id; // Set by cheeze
	int buf_len; // Set by koo
	char *buf; // Set by koo
	int ubuf_len; // Set by bom
	char __user *ubuf; // Set by bom
	char *ret_buf; // Set by koo (Could be NULL)
} __attribute__((aligned(8), packed));

#ifdef __KERNEL__

#include <linux/list.h>

struct cheeze_queue_item {
	int id;
	struct list_head tag_list;
};

struct cheeze_req {
	struct completion acked; // Set by cheeze
	bool sync;
	bool transfer;
	void *extra;
	struct cheeze_req_user *user; // Set by koo, needs to be freed by koo
	struct cheeze_queue_item *item;
};

// blk.c
uint64_t cheeze_prepare_io(struct cheeze_req_user *user, char sync, void *extra, bool transfer);
void cheeze_free_io(int id);
void cheeze_io(struct cheeze_req_user *user, void *(*cb)(void *data), void *extra, uint64_t seq); // Called by koo
extern struct class *cheeze_chr_class;
// extern struct mutex cheeze_mutex;
void cheeze_chr_cleanup_module(void);
int cheeze_chr_init_module(void);

// queue.c
extern struct cheeze_req *reqs;
uint64_t cheeze_push(struct cheeze_req_user *user);
struct cheeze_req *cheeze_peek(void);
void cheeze_pop(int id);
void cheeze_move_pop(int id);
void cheeze_queue_init(void);
void cheeze_queue_exit(void);

//shm.c
int send_req (struct cheeze_req *req, int id, uint64_t seq);
static inline char *get_buf_addr(char **pdata_addr, int id) {
	int idx = id / ITEMS_PER_HP;
	return pdata_addr[idx] + ((id % ITEMS_PER_HP) * CHEEZE_BUF_SIZE);
}
void shm_init(void);
void shm_exit(void);

#endif

#endif

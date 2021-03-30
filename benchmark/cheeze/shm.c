// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Park Ju Hyung
 */

#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/backing-dev.h>
#include <linux/blk-mq.h>

#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include "cheeze.h"

static void *page_addr[3];
//static void *meta_addr; // page_addr[0] ==> send_event_addr, recv_event_addr, seq_addr, ureq_addr
static uint8_t *send_event_addr; // CHEEZE_QUEUE_SIZE ==> 16B
static uint8_t *recv_event_addr; // 16B
static uint64_t *seq_addr; // 8KB
static struct cheeze_req_user *ureq_addr; // sizeof(req) * 1024
void *cheeze_data_addr[2]; // page_addr[1]: 1GB, page_addr[2]: 1GB

static struct task_struct *shm_task = NULL;

static void shm_meta_init(void *ppage_addr);
static void shm_data_init(void **ppage_addr);

static unsigned long delay_us;
module_param(delay_us, ulong, 0644);

int cheeze_do_request(struct cheeze_req *req)
{
	unsigned long b_len = 0;
	struct bio_vec bvec;
	struct req_iterator iter;
	loff_t off = 0;
	void *bbuf, *ubuf;
	struct request *rq;

	if (delay_us)
		udelay(delay_us);

	rq = req->rq;
	ubuf = get_buf_addr(req->user.id);

	pr_debug("%s++\n", __func__);

	/* Iterate over all requests segments */
	rq_for_each_segment(bvec, rq, iter) {
		b_len = bvec.bv_len;

		/* Get pointer to the data */
		bbuf = page_address(bvec.bv_page) + bvec.bv_offset;

		pr_debug("off: %lld, len: %ld, dest_buf: %px, user_buf: %px\n", off, b_len, bbuf, ubuf);

		switch (req->user.op) {
		case REQ_OP_WRITE:
			// Write
			memcpy(ubuf + off, bbuf, 1 << CHEEZE_LOGICAL_BLOCK_SHIFT);
			break;
		case REQ_OP_READ:
			// Read
			memcpy(bbuf, ubuf + off, 1 << CHEEZE_LOGICAL_BLOCK_SHIFT);
			break;
		}

		/* Increment counters */
		off += b_len;
	}

	pr_debug("%s--\n", __func__);

	return 0;
}

static void do_request(struct cheeze_req *req)
{
	// Process bio
	if (likely(req->is_rw) && req->user.op == READ)
		req->ret = cheeze_do_request(req);
	else
		req->ret = 0;

	blk_mq_end_request(req->rq, req->ret < 0 ? BLK_STS_IOERR : BLK_STS_OK);
	cheeze_move_pop(req->id);
	//complete(&req->acked);
}

int send_req (struct cheeze_req *req, int id, uint64_t seq) {
	uint8_t *send =&send_event_addr[id];
	// char *buf = get_buf_addr(id);
	// struct cheeze_req_user *ureq = ureq_addr + id;
	// caller should be call memcpy to reqs before calling this function
	memcpy(ureq_addr + id, &req->user, sizeof(struct cheeze_req_user));
	// ??? memcpy(ureq, req->user, sizeof(*ureq));
	seq_addr[id] = seq;
	pr_debug("%s: id = %d, seq = %llu\n", __func__, id, seq);
	/* memory barrier XXX:Arm */
	//*send = *send | (1ULL << (id % BITS_PER_EVENT));
	barrier();
	*send = 1;
	/* memory barrier XXX:Arm */
	return 0;
}

static void recv_req (void) {
	uint8_t *recv;
	int i, id, j;
	struct cheeze_req *req;

	for (i = 0; i < CHEEZE_QUEUE_SIZE; i++) {
		recv = recv_event_addr + i;
		if (*recv) {
			id = i;
			pr_debug("%s: id = %d (i: %d, j: %d)\n", __func__, id, i, j);
			req = reqs + id;
			// XXX: Optimize with zerocopy
			memcpy(&req->user, ureq_addr + id, sizeof(struct cheeze_req_user));
			ureq_print(req->user);
			do_request(req);
				/* memory barrier XXX:Arm */
			barrier();
			*recv = 0;
			/* memory barrier XXX:Arm */
		}
	}
}

static int shm_kthread(void *unused)
{
	while (!kthread_should_stop()) {
		recv_req();
		cond_resched();
	}

	return 0;
}

static int set_page_addr0(const char *val, const struct kernel_param *kp)
{
	unsigned long dst;
	int ret;

	if (strncmp(val, "0x", 2))
		ret = kstrtoul(val, 16, &dst);
	else
		ret = kstrtoul(val + 2, 16, &dst);

	if (ret < 0)
		return ret;

	page_addr[0] = phys_to_virt(dst);;
	pr_info("page_addr[0]: 0x%px\n", page_addr[0]);

	return ret;
}

const struct kernel_param_ops page_addr_ops0 = {
	.set = set_page_addr0,
	.get = NULL
};

module_param_cb(page_addr0, &page_addr_ops0, NULL, 0644);

static int set_page_addr1(const char *val, const struct kernel_param *kp)
{
	unsigned long dst;
	int ret;

	if (strncmp(val, "0x", 2))
		ret = kstrtoul(val, 16, &dst);
	else
		ret = kstrtoul(val + 2, 16, &dst);

	if (ret < 0)
		return ret;

	page_addr[1] = phys_to_virt(dst);;
	pr_info("page_addr[1]: 0x%px\n", page_addr[1]);

	return ret;
}

const struct kernel_param_ops page_addr_ops1 = {
	.set = set_page_addr1,
	.get = NULL
};

module_param_cb(page_addr1, &page_addr_ops1, NULL, 0644);

static int set_page_addr2(const char *val, const struct kernel_param *kp)
{
	unsigned long dst;
	int ret;

	if (strncmp(val, "0x", 2))
		ret = kstrtoul(val, 16, &dst);
	else
		ret = kstrtoul(val + 2, 16, &dst);

	if (ret < 0)
		return ret;

	page_addr[2] = phys_to_virt(dst);;
	pr_info("page_addr[2]: 0x%px\n", page_addr[2]);

	shm_meta_init(page_addr[0]);
	shm_data_init(page_addr);

	return ret;
}

const struct kernel_param_ops page_addr_ops2 = {
	.set = set_page_addr2,
	.get = NULL
};

module_param_cb(page_addr2, &page_addr_ops2, NULL, 0644);

static bool enable;
static int enable_param_set(const char *val, const struct kernel_param *kp)
{
	int ret;

	if (page_addr[0] == NULL)
		return -EINVAL;

	ret = param_set_bool(val, kp);

	if (enable) {
		pr_info("Enabling shm\n");
		shm_task = kthread_run(shm_kthread, NULL, "kshm");
		pr_info("Enabled shm\n");
	} else {
		pr_info("Disabling shm\n");
		kthread_stop(shm_task);
		shm_task = NULL;
		pr_info("Disabled shm\n");
	}

	return ret;
}

static struct kernel_param_ops enable_param_ops = {
	.set = enable_param_set,
	.get = param_get_bool,
};

module_param_cb(enabled, &enable_param_ops, &enable, 0644);

static void shm_meta_init(void *ppage_addr) {
	memset(ppage_addr, 0, HP_SIZE);
	send_event_addr = ppage_addr + SEND_OFF; // CHEEZE_QUEUE_SIZE ==> 16B
	recv_event_addr = ppage_addr + RECV_OFF; // 16B
	seq_addr = ppage_addr + SEQ_OFF; // 8KB
	ureq_addr = ppage_addr + REQS_OFF; // sizeof(req) * 1024
}

static void shm_data_init(void **ppage_addr) {
	cheeze_data_addr[0] = ppage_addr[1];
	cheeze_data_addr[1] = ppage_addr[2];
}

void __exit shm_exit(void)
{
	if (!shm_task)
		return;

	kthread_stop(shm_task);
	shm_task = NULL;
}

//module_exit(shm_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Park Ju Hyung <qkrwngud825@gmail.com>");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Park Ju Hyung
 */


#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include "cheeze.h"
#include "../lightfs_fs.h"

static void *page_addr[3];
//static void *meta_addr; // page_addr[0] ==> send_event_addr, recv_event_addr, seq_addr, ureq_addr
static uint8_t *send_event_addr; // CHEEZE_QUEUE_SIZE ==> 16B
static uint8_t *recv_event_addr; // 16B
static uint64_t *seq_addr; // 8KB
struct cheeze_req_user *ureq_addr; // sizeof(req) * 1024
char *data_addr[2]; // page_addr[1]: 1GB, page_addr[2]: 1GB

static struct task_struct *shm_task = NULL;
static void shm_meta_init(void *ppage_addr);
static void shm_data_init(void **ppage_addr);

int send_req (struct cheeze_req *req, int id, uint64_t seq) {
	uint8_t *send = &send_event_addr[id];
	char *buf = get_buf_addr(data_addr, id);
	struct cheeze_req_user *ureq = ureq_addr + id;
	// caller should be call memcpy to reqs before calling this function
	memcpy(buf, req->user->buf, req->user->buf_len);
	memcpy(ureq, req->user, sizeof(struct cheeze_req_user));
	seq_addr[id] = seq;
	/* memory barrier XXX:Arm */
	barrier();
	//pr_info("[send req] send: %d\n", send_event_addr[id]);
	if (*send) {
		pr_info("already used!!!\n");
	}
	*send = 1;
	//pr_info("[send req] user %p, user->id: %d user->buf_len: %d, user->buf: %p, user->ret_buf: %p\n", req->user, req->user->id, req->user->buf_len, req->user->buf, req->user->ret_buf);
	//pr_info("[send req] SEND[%d]: %d, ID: %d, SEQ: %d, ureq %p, ureq->id: %d ureq->buf_len: %d, ureq->buf: %p, ureq->ret_buf: %p\n", id, send[id], id, seq, ureq, ureq->id, ureq->buf_len, ureq->buf, ureq->ret_buf);
	/* memory barrier XXX:Arm */
	return 0;
}

static void recv_req (void) {
	uint8_t *recv;
	int i, id;
	struct cheeze_req *req;
	struct cheeze_req_user *ureq;
	char *buf;

	for (i = 0; i < CHEEZE_QUEUE_SIZE; i++) {
		recv = &recv_event_addr[i];
		if (*recv) {
			id = i;
			req = reqs + id;
			ureq = ureq_addr + id;
			buf = get_buf_addr(data_addr, id);
			//if (!req->sync && !req->extra) {
			//	*recv = 0;
				//cheeze_move_pop(id);
				//memset(ureq, 0, sizeof(struct cheeze_req_user));
			//	continue;
			//}
			if (req->extra && ureq->ubuf_len != 0) {
				struct reada_entry *ra_entry = (struct reada_entry *)(req->extra);
				struct lightfs_inode *lightfs_inode = (struct lightfs_inode *)(ra_entry->extra);
				spin_lock(&lightfs_inode->reada_spin);
				ra_entry->reada_state |= READA_DONE;
				complete_all(&ra_entry->reada_acked);
				spin_unlock(&lightfs_inode->reada_spin);
			}
			if (ureq->ret_buf == NULL || !req->sync || req->transfer) { // SET, TRANSFER
				//memcpy(req->user, ureq, sizeof(struct cheeze_req_user));
				if (!req->sync) {
					cheeze_move_pop(id);
				} else {
					complete(&req->acked);
					cheeze_move_pop(id);
				}
			} else {
				if (ureq->ubuf_len != 0) { // GET
					//pr_info("[recv req] req->extra: %p\n", req->extra);
				//pr_info("[recv req] user %p, user->id: %d user->buf_len: %d, user->buf: %p, user->ret_buf: %p user->ubuf_len: %d\n", req->user, req->user->id, req->user->buf_len, req->user->buf, req->user->ret_buf, req->user->ubuf_len);
				//memcpy(req->user, ureq, sizeof(struct cheeze_req_user));
					if (req->extra != NULL) {
#ifdef READA
						struct reada_entry *ra_entry = (struct reada_entry *)(req->extra);
						struct lightfs_inode *lightfs_inode = (struct lightfs_inode *)(ra_entry->extra);
						spin_lock(&lightfs_inode->reada_spin);
						ra_entry->reada_state |= READA_DONE;
						complete_all(&ra_entry->reada_acked);
						spin_unlock(&lightfs_inode->reada_spin);
#endif

					} else {
						if (req->user->ubuf_len == 152) {
							memcpy(req->user, ureq, sizeof(struct cheeze_req_user));
							req->user->ubuf_len = 152;
						} else {
							memcpy(req->user, ureq, sizeof(struct cheeze_req_user));
						}
						complete(&req->acked);
					}

				//pr_info("[recv req] ureq %p, ureq->id: %d ureq->buf_len: %d, ureq->buf: %p, ureq->ret_buf: %p user->ubuf_len: %d\n", ureq, ureq->id, ureq->buf_len, ureq->buf, ureq->ret_buf, ureq->ubuf_len);
					//memcpy(ureq->ret_buf, buf, req->user->ubuf_len);
				} else {
					memcpy(req->user, ureq, sizeof(struct cheeze_req_user));
					complete(&req->acked);
				}
			}
			/* memory barrier XXX:Arm */
			//cheeze_move_pop(id);
			//memset(ureq, 0, sizeof(struct cheeze_req_user));
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

#if 0
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

	//shm_meta_init(page_addr[0]);
	//shm_data_init(page_addr);

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

	if (page_addr == NULL)
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
#endif

void shm_init() {
#ifdef IS_IN_VM
	page_addr[0] = ioremap_nocache(SHM_ADDR0, HP_SIZE);
	page_addr[1] = ioremap_nocache(SHM_ADDR1, HP_SIZE);
	page_addr[2] = ioremap_nocache(SHM_ADDR2, HP_SIZE);
#else
	page_addr[0] = phys_to_virt(SHM_ADDR0);
	page_addr[1] = phys_to_virt(SHM_ADDR1);
	page_addr[2] = phys_to_virt(SHM_ADDR2);
#endif
	shm_meta_init(page_addr[0]);
	shm_data_init(page_addr);
	shm_task = kthread_run(shm_kthread, NULL, "kshm");
}

static void shm_meta_init(void *ppage_addr) {
	memset(ppage_addr, 0, (1ULL * 1024 * 1024 * 1024));
	send_event_addr = ppage_addr + SEND_OFF; // CHEEZE_QUEUE_SIZE ==> 1024B
	recv_event_addr = ppage_addr + RECV_OFF; // 1024B
	seq_addr = ppage_addr + SEQ_OFF; // 8KB
	ureq_addr = ppage_addr + REQS_OFF; // sizeof(req) * 1024
}

static void shm_data_init(void **ppage_addr) {
	data_addr[0] = ppage_addr[1];
	data_addr[1] = ppage_addr[2];
	memset(data_addr[0], 0, (1ULL * 1024 * 1024 * 1024));
	memset(data_addr[1], 0, (1ULL * 1024 * 1024 * 1024));
}

void shm_exit(void)
{
	if (!shm_task)
		return;

	kthread_stop(shm_task);
	shm_task = NULL;
}

//module_exit(shm_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Park Ju Hyung <qkrwngud825@gmail.com>");

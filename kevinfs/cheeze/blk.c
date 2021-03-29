// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Park Ju Hyung
 */

#define pr_fmt(fmt) "cheeze: " fmt

/*
 * XXX
 ** Concurrent I/O may require rwsem lock
 */

// #define DEBUG

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/backing-dev.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/blk-mq.h>

#include "cheeze.h"

/* Globals */
struct class *cheeze_chr_class;

extern char *data_addr[2];

uint64_t cheeze_prepare_io(struct cheeze_req_user *user, char sync, void *extra, bool transfer) {
	int id;
	uint64_t seq;
	struct cheeze_req *req;
	
	seq = cheeze_push(user);
	id = user->id;
	req = reqs + id;

	user->buf = get_buf_addr(data_addr, id);
	req->sync = sync;
	req->transfer = transfer;
	req->extra = extra;

	return seq;
}

void cheeze_free_io(int id) {
	cheeze_move_pop(id);
}

/* Serve requests from koo */
void cheeze_io(struct cheeze_req_user *user, void *(*cb)(void *data), void *extra, uint64_t seq) 
{
	int id;
	struct cheeze_req *req;

	id = user->id;

	req = reqs + id;

	send_req(req, id, seq);

	if (cb) {
		cb(extra);
	}

	if (req->sync)
		wait_for_completion(&req->acked);
	//cheeze_move_pop(id);
}
EXPORT_SYMBOL(cheeze_io);

int cheeze_init(void)
{
	int ret, i;

	/*
	cheeze_chr_class = class_create(THIS_MODULE, "cheeze_chr");
	if (IS_ERR(cheeze_chr_class)) {
		ret = PTR_ERR(cheeze_chr_class);
		pr_warn("Failed to register class cheeze_chr\n");
		goto out;
	}

	ret = cheeze_chr_init_module();
	if (ret)
		goto destroy_chr;

	*/
	reqs = kzalloc(sizeof(struct cheeze_req) * CHEEZE_QUEUE_SIZE, GFP_KERNEL);
	if (reqs == NULL) {
		pr_err("%s %d: Unable to allocate memory for cheeze_req\n", __func__, __LINE__);
		ret = -ENOMEM;
		//goto nomem;
	}
	cheeze_queue_init();
	for (i = 0; i < CHEEZE_QUEUE_SIZE; i++)
		init_completion(&reqs[i].acked);

	shm_init();

	return 0;
/*
nomem:
	cheeze_chr_cleanup_module();
destroy_chr:
	class_destroy(cheeze_chr_class);
out:
*/
	return ret;
}

void cheeze_exit(void)
{
	shm_exit();
	cheeze_queue_exit();
	kfree(reqs);

	//cheeze_chr_cleanup_module();
	//class_destroy(cheeze_chr_class);
}

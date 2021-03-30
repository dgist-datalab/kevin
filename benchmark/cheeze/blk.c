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

// cheeze is intentionally designed to expose 1 disk only

/* Globals */
static int cheeze_major;
static struct gendisk *cheeze_disk;
static u64 cheeze_disksize;
static struct page *swap_header_page;
static struct blk_mq_tag_set tag_set;

struct class *cheeze_chr_class;

static int cheeze_open(struct block_device *dev, fmode_t mode)
{
	pr_info("%s\n", __func__);
	return 0;
}

static void cheeze_release(struct gendisk *gdisk, fmode_t mode)
{
	pr_info("%s\n", __func__);
}

static int cheeze_ioctl(struct block_device *bdev, fmode_t mode, unsigned cmd,
		   unsigned long arg)
{
	pr_info("ioctl cmd 0x%08x\n", cmd);

	return -ENOTTY;
}

/* Serve requests */
static int do_request(struct request *rq)
{
	int ret, id;
	uint64_t seq;
	struct cheeze_req *req;

	seq = cheeze_push(rq, &req);
	id = req->user.id;
	if (unlikely(id < 0)) {
		if (id == SKIP)
			return 0;
		return id;
	}

	if (req->user.op == WRITE)
		cheeze_do_request(req);

	send_req(req, id, seq);

	//wait_for_completion(&req->acked);

	//ret = req->ret;
	req->id = id;
	ret = 0;
	//cheeze_move_pop(id);

	return ret;
}

/* queue callback function */
static blk_status_t queue_rq(struct blk_mq_hw_ctx *hctx,
			     const struct blk_mq_queue_data *bd)
{
	int ret;
	struct request *rq = bd->rq;

	/* Start request serving procedure */
	blk_mq_start_request(rq);

	ret = do_request(rq);

	/* Stop request serving procedure */
	//blk_mq_end_request(rq, ret < 0 ? BLK_STS_IOERR : BLK_STS_OK);

	return ret;
}

static const struct blk_mq_ops mq_ops = {
	.queue_rq = queue_rq,
};

static const struct block_device_operations cheeze_fops = {
	.owner = THIS_MODULE,
	.open = cheeze_open,
	.release = cheeze_release,
	.ioctl = cheeze_ioctl
};

static ssize_t disksize_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", cheeze_disksize);
}

static ssize_t disksize_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t len)
{
	int ret;
	u64 disksize;

	ret = kstrtoull(buf, 10, &disksize);
	if (ret)
		return ret;

	if (disksize == 0) {
		set_capacity(cheeze_disk, 0);
		return len;
	}

	cheeze_disksize = PAGE_ALIGN(disksize);
	if (!cheeze_disksize) {
		pr_err("disksize is invalid (disksize = %llu)\n", cheeze_disksize);

		cheeze_disksize = 0;

		return -EINVAL;
	}

	set_capacity(cheeze_disk, cheeze_disksize >> SECTOR_SHIFT);

	return len;
}

static DEVICE_ATTR(disksize, S_IRUGO | S_IWUSR, disksize_show, disksize_store);

static struct attribute *cheeze_disk_attrs[] = {
	&dev_attr_disksize.attr,
	NULL,
};

static const struct attribute_group cheeze_disk_attr_group = {
	.attrs = cheeze_disk_attrs,
};

#ifndef blk_mq_init_sq_queue
/*
 * Helper for setting up a queue with mq ops, given queue depth, and
 * the passed in mq ops flags.
 */
static struct request_queue *blk_mq_init_sq_queue(struct blk_mq_tag_set *set,
                                          const struct blk_mq_ops *ops,
                                          unsigned int queue_depth,
                                          unsigned int set_flags)
{
       struct request_queue *q;
       int ret;

       memset(set, 0, sizeof(*set));
       set->ops = ops;
       set->nr_hw_queues = 1;
       set->queue_depth = queue_depth;
       set->numa_node = NUMA_NO_NODE;
       set->flags = set_flags;

       ret = blk_mq_alloc_tag_set(set);
       if (ret)
               return ERR_PTR(ret);

       q = blk_mq_init_queue(set);
       if (IS_ERR(q)) {
               blk_mq_free_tag_set(set);
               return q;
       }

       return q;
}
#endif

static int create_device(void)
{
	int ret;

	/* gendisk structure */
	cheeze_disk = alloc_disk(1);
	if (!cheeze_disk) {
		pr_err("%s %d: Error allocating disk structure for device\n",
		       __func__, __LINE__);
		ret = -ENOMEM;
		goto out;
	}

	cheeze_disk->queue = blk_mq_init_sq_queue(&tag_set, &mq_ops, 1024, BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_SG_MERGE);
	if (!cheeze_disk->queue) {
		pr_err("%s %d: Error allocating disk queue for device\n",
		       __func__, __LINE__);
		ret = -ENOMEM;
		goto out_put_disk;
	}

	// blk_queue_make_request(cheeze_disk->queue, cheeze_make_request);

	cheeze_disk->major = cheeze_major;
	cheeze_disk->first_minor = 0;
	cheeze_disk->fops = &cheeze_fops;
	cheeze_disk->private_data = NULL;
	snprintf(cheeze_disk->disk_name, 16, "cheeze%d", 0);

	/* Actual capacity set using sysfs (/sys/block/cheeze<id>/disksize) */
	set_capacity(cheeze_disk, 0);

	/*
	 * To ensure that we always get PAGE_SIZE aligned
	 * and n*PAGE_SIZED sized I/O requests.
	 */
	blk_queue_physical_block_size(cheeze_disk->queue, PAGE_SIZE);
	blk_queue_logical_block_size(cheeze_disk->queue,
				     CHEEZE_LOGICAL_BLOCK_SIZE);
	blk_queue_io_min(cheeze_disk->queue, PAGE_SIZE);
	blk_queue_max_hw_sectors(cheeze_disk->queue, 4096); // 512 * 4096 = 2MiB

	// Set discard capability
	cheeze_disk->queue->limits.discard_granularity = PAGE_SIZE;
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, cheeze_disk->queue);
	blk_queue_max_discard_sectors(cheeze_disk->queue, 4096); // 512 * 4096 = 2MiB
	blk_queue_max_write_zeroes_sectors(cheeze_disk->queue, 4096); // 512 * 4096 = 2MiB

	add_disk(cheeze_disk);

	cheeze_disksize = 0;

	ret = sysfs_create_group(&disk_to_dev(cheeze_disk)->kobj,
				 &cheeze_disk_attr_group);
	if (ret < 0) {
		pr_err("%s %d: Error creating sysfs group\n",
		       __func__, __LINE__);
		goto out_free_queue;
	}

	/* cheeze devices sort of resembles non-rotational disks */
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, cheeze_disk->queue);
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, cheeze_disk->queue);

out:
	return ret;

out_free_queue:
	blk_cleanup_queue(cheeze_disk->queue);

out_put_disk:
	put_disk(cheeze_disk);

	return ret;
}

static void destroy_device(void)
{
	if (!cheeze_disk)
		return;

	pr_info("Removing device cheeze0\n");

	sysfs_remove_group(&disk_to_dev(cheeze_disk)->kobj,
			   &cheeze_disk_attr_group);

	if (cheeze_disk->queue)
		blk_cleanup_queue(cheeze_disk->queue);

	del_gendisk(cheeze_disk);
	put_disk(cheeze_disk);

	cheeze_disk = NULL;
}

static int __init cheeze_init(void)
{
	int ret, i;

	cheeze_major = register_blkdev(0, "cheeze");
	if (cheeze_major <= 0) {
		pr_err("%s %d: Unable to get major number\n",
		       __func__, __LINE__);
		ret = -EBUSY;
		goto out;
	}

	ret = create_device();
	if (ret) {
		pr_err("%s %d: Unable to create cheeze_device\n",
		       __func__, __LINE__);
		goto free_devices;
	}

	reqs = kzalloc(sizeof(struct cheeze_req) * CHEEZE_QUEUE_SIZE, GFP_KERNEL);
	if (reqs == NULL) {
		pr_err("%s %d: Unable to allocate memory for cheeze_req\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto nomem;
	}
	cheeze_queue_init();
	//for (i = 0; i < CHEEZE_QUEUE_SIZE; i++)
	//	init_completion(&reqs[i].acked);

	return 0;

nomem:
	destroy_device();
free_devices:
	unregister_blkdev(cheeze_major, "cheeze");
out:
	return ret;
}

static void __exit cheeze_exit(void)
{
	cheeze_queue_exit();

	kfree(reqs);

	shm_exit();

	destroy_device();

	unregister_blkdev(cheeze_major, "cheeze");

	if (swap_header_page)
		__free_page(swap_header_page);
}

module_init(cheeze_init);
module_exit(cheeze_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Park Ju Hyung <qkrwngud825@gmail.com>");

#include "lightfs_reada.h"
#include "./cheeze/cheeze.h"
#include "lightfs.h"

struct reada_entry *lightfs_reada_alloc(struct inode *inode, uint64_t current_block_num, unsigned block_cnt) {
	struct reada_entry *ra_entry = kmalloc(sizeof(struct reada_entry), GFP_KERNEL);
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);

	//BUG_ON(ra_entry->reada_state & READA_FULL);

	ra_entry->reada_state = READA_FULL;
	ra_entry->reada_block_start = current_block_num;
	ra_entry->reada_block_len = block_cnt;
	ra_entry->tag = 0;
	init_completion(&ra_entry->reada_acked);
	ra_entry->extra = lightfs_inode;

	INIT_LIST_HEAD(&ra_entry->list);
	list_add_tail(&ra_entry->list, &lightfs_inode->ra_list);
	lightfs_inode->ra_entry_cnt++;
	if (lightfs_inode->ra_entry_cnt == 1) {
		BUG_ON(lightfs_inode->ra_entry);
		lightfs_inode->ra_entry = ra_entry;
	}

	return ra_entry;
}

void lightfs_reada_free(struct reada_entry *ra_entry, struct inode *inode) {
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
	struct reada_entry *tmp = NULL;
	//BUG_ON(!(ra_entry->reada_state & READA_DONE));
	spin_lock(&lightfs_inode->reada_spin);
	if (!(ra_entry->reada_state & READA_DONE)) {
		spin_unlock(&lightfs_inode->reada_spin);
		wait_for_completion(&ra_entry->reada_acked);
		spin_lock(&lightfs_inode->reada_spin);
	}
	cheeze_free_io(lightfs_inode->ra_entry->tag);
	list_del(&ra_entry->list);
	lightfs_inode->ra_entry_cnt--;
	if (lightfs_inode->ra_entry_cnt == 0) {
		lightfs_inode->ra_entry = NULL;
	} else {
		lightfs_inode->ra_entry = list_first_entry(&lightfs_inode->ra_list, struct reada_entry, list);
	}
	spin_unlock(&lightfs_inode->reada_spin);
	kfree(tmp);
}

void lightfs_reada_all_flush(struct inode *inode) {
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
	struct reada_entry *ra_entry, *next;

	list_for_each_entry_safe(ra_entry, next, &lightfs_inode->ra_list, list) {
		lightfs_reada_free(ra_entry, inode);
	}
}

void lightfs_reada_flush(struct inode *inode, int cnt) {
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
	struct reada_entry *ra_entry, *next;
	volatile int i = 0;

	list_for_each_entry_safe(ra_entry, next, &lightfs_inode->ra_list, list) {
		if (cnt == i)
			break;
		lightfs_reada_free(ra_entry, inode);
		i++;
	}
}


struct reada_entry *lightfs_reada_reuse(struct inode *inode, uint64_t current_block_num, unsigned block_cnt) {
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
	struct reada_entry *ra_entry = lightfs_inode->ra_entry;

	//BUG_ON(lightfs_inode->reada_state & READA_EMPTY);

	cheeze_free_io(ra_entry->tag);

	ra_entry->reada_state = READA_FULL;
	ra_entry->reada_block_start = current_block_num;
	ra_entry->reada_block_len = block_cnt;
	ra_entry->tag = 0;
	reinit_completion(&ra_entry->reada_acked);
	ra_entry->extra = lightfs_inode;

	spin_lock(&lightfs_inode->reada_spin);
	list_move_tail(&ra_entry->list, &lightfs_inode->ra_list);
	spin_unlock(&lightfs_inode->reada_spin);

	return ra_entry;
}


bool lightfs_reada_need(struct inode *inode, struct lightfs_io *lightfs_io, unsigned nr_pages, bool fg_read) {
	uint64_t current_start_block;
	bool ret = false;
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);

	if (nr_pages < READA_THRESHOLD) {
		return false;
	}

	current_start_block = PAGE_TO_BLOCK_NUM(lightfs_io_first_page(lightfs_io));

	spin_lock(&lightfs_inode->reada_spin);
	if (lightfs_inode->ra_entry_cnt >= READA_QD) {
		ret = false;
	} else {
		if (fg_read) {
			if(current_start_block == lightfs_inode->last_block_start + lightfs_inode->last_block_len) {
				ret = true;
			}
		} else if (lightfs_inode->ra_entry) {
			if (current_start_block + nr_pages == lightfs_inode->ra_entry->reada_block_start + lightfs_inode->ra_entry->reada_block_len) {
				ret = true;
			}
		}
	}
	lightfs_inode->last_block_start = current_start_block;
	lightfs_inode->last_block_len = nr_pages;
	spin_unlock(&lightfs_inode->reada_spin);
	return ret;
}

#if 0
static bool lightfs_reada_wait_finished(struct inode *inode) {
	bool ret;
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
	spin_lock(&lightfs_inode->reada_spin);
	ret = lightfs_inode->ra_entry->reada_state & READA_DONE;
	spin_unlock(&lightfs_inode->reada_spin);
	return ret;
}
#endif

unsigned lightfs_reada_buffer_get(struct reada_entry *ra_entry, struct inode *inode, struct lightfs_io *lightfs_io, unsigned nr_pages) {
	unsigned ret;
	unsigned processed_pages = 0;
	uint64_t current_block_num;
	struct page *page;
	char *page_buf;
	int idx;
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
	
	if (!lightfs_inode->ra_entry) {
		ret = ENORA;
		goto out;
	}

	if (nr_pages < READA_THRESHOLD) {
		ret = ESMALL;
		goto out;
	}


	spin_lock(&lightfs_inode->reada_spin);
	if (ra_entry->reada_state & READA_EMPTY || nr_pages < READA_THRESHOLD) {
		spin_unlock(&lightfs_inode->reada_spin);
		ret = ENORA;
		goto out;
	}

	current_block_num = PAGE_TO_BLOCK_NUM(lightfs_io_current_page(lightfs_io));
	if (current_block_num >=  ra_entry->reada_block_start) {
		idx = current_block_num - ra_entry->reada_block_start;
		if (current_block_num + nr_pages <= ra_entry->reada_block_start + ra_entry->reada_block_len) { // buf is enough to serve requests
			if (!(ra_entry->reada_state & READA_DONE)) {
				spin_unlock(&lightfs_inode->reada_spin);
				wait_for_completion(&ra_entry->reada_acked);
				spin_lock(&lightfs_inode->reada_spin);
			}
			BUG_ON(!(ra_entry->reada_state & READA_DONE));
			while (!lightfs_io_job_done(lightfs_io)) {
				page = lightfs_io_current_page(lightfs_io);
				page_buf = kmap_atomic(page);
				memcpy(page_buf, ra_entry->buf + idx, PAGE_SIZE);
				kunmap_atomic(page_buf);
				lightfs_io_advance_page(lightfs_io);
				idx++;
				processed_pages++;
			}
		} else if (current_block_num < ra_entry->reada_block_start + ra_entry->reada_block_len) { // buf is not enough
			if (!(ra_entry->reada_state & READA_DONE)) {
				spin_unlock(&lightfs_inode->reada_spin);
				wait_for_completion(&ra_entry->reada_acked);
				spin_lock(&lightfs_inode->reada_spin);
			}
			BUG_ON(!(ra_entry->reada_state & READA_DONE));
			while(idx < ra_entry->reada_block_len) {
				page = lightfs_io_current_page(lightfs_io);
				page_buf = kmap_atomic(page);
				memcpy(page_buf, ra_entry->buf + idx, PAGE_SIZE);
				kunmap_atomic(page_buf);
				lightfs_io_advance_page(lightfs_io);
				idx++;
				processed_pages++;
			}
		} else { // buffer miss
			processed_pages = 0;
		}
	} else { // buffer miss
		processed_pages = 0;
	}
	ret = processed_pages;
	spin_unlock(&lightfs_inode->reada_spin);
out:
	return ret;
}

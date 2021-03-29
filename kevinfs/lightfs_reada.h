#ifndef __READA_H__
#define __READA_H__

#include "lightfs_fs.h"

struct reada_entry *lightfs_reada_alloc(struct inode *inode, uint64_t current_block_num, unsigned block_cnt);
void lightfs_reada_free(struct reada_entry *ra_entry, struct inode *inode);
void lightfs_reada_all_flush(struct inode *inode);
void lightfs_reada_flush(struct inode *inode, int cnt);
struct reada_entry *lightfs_reada_reuse(struct inode *inode, uint64_t current_block_num, unsigned block_cnt);
bool lightfs_reada_need(struct inode *inode, struct lightfs_io *lightfs_io, unsigned nr_pages, bool fg_read);
unsigned lightfs_reada_buffer_get(struct reada_entry *ra_entry, struct inode *inode, struct lightfs_io *lightfs_io, unsigned nr_pages);
#endif

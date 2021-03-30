/* -*- mode: C++; c-basic-offset: 8; indent-tabs-mode: t -*- */
// vim: set tabstop=8 softtabstop=8 shiftwidth=8 noexpandtab:

#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/parser.h>
#include <linux/list_sort.h>
#include <linux/writeback.h>
#include <linux/path.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/quotaops.h>

#include "lightfs_fs.h"
#include "lightfs.h"
#include "lightfs_reada.h"

static char root_meta_key[] = "m\x00\x00\x00\x00\x00\x00\x00\x00";

static struct kmem_cache *lightfs_inode_cachep;

/*
 * lightfs_i_init_once is passed to kmem_cache_create
 * Once an inode is allocated, this function is called to init that inode
 */
static void lightfs_i_init_once(void *inode)
{
	struct lightfs_inode *lightfs_inode = inode;

	dbt_init(&lightfs_inode->meta_dbt);

	inode_init_once(&lightfs_inode->vfs_inode);
}

static void
lightfs_setup_metadata(struct lightfs_metadata *meta, umode_t mode,
                    loff_t size, dev_t rdev, ino_t ino)
{
	struct timespec now_tspec;
	time_t now;

	now_tspec = current_kernel_time();
	TIMESPEC_TO_TIME_T(now, now_tspec);

	meta->type = LIGHTFS_METADATA_TYPE_NORMAL;
	meta->u.st.st_dev = 0;
	meta->u.st.st_ino = ino;
	meta->u.st.st_mode = mode;
	meta->u.st.st_size = size;
	meta->u.st.st_nlink = 1;
#ifdef CONFIG_UIDGID_STRICT_TYPE_CHECKS
	meta->u.st.st_uid = current_uid().val;
	meta->u.st.st_gid = current_gid().val;
#else
	//meta->u.st.st_uid = current_uid();
	//meta->u.st.st_gid = current_gid();
	meta->u.st.st_uid = from_kuid_munged(current_user_ns(), current_uid());
	meta->u.st.st_gid = from_kgid_munged(current_user_ns(), current_gid());
#endif
	meta->u.st.st_rdev = rdev;
	meta->u.st.st_blocks = lightfs_get_block_num_by_size(size);
	meta->u.st.st_blksize = LIGHTFS_BSTORE_BLOCKSIZE;
	meta->u.st.st_atime = now;
	meta->u.st.st_mtime = now;
	meta->u.st.st_ctime = now;
}

static void
lightfs_copy_metadata_from_inode(struct lightfs_metadata *meta, struct inode *inode)
{
	meta->type = LIGHTFS_METADATA_TYPE_NORMAL;
	meta->u.st.st_dev = 0;
	meta->u.st.st_ino = inode->i_ino;
	meta->u.st.st_mode = inode->i_mode;
	meta->u.st.st_nlink = inode->i_nlink;
#ifdef CONFIG_UIDGID_STRICT_TYPE_CHECKS
	meta->u.st.st_uid = inode->i_uid.val;
	meta->u.st.st_gid = inode->i_gid.val;
#else
	//meta->u.st.st_uid = inode->i_uid;
	//meta->u.st.st_gid = inode->i_gid;
	meta->u.st.st_uid = from_kuid_munged(inode->i_sb->s_user_ns, inode->i_uid);
	meta->u.st.st_gid = from_kgid_munged(inode->i_sb->s_user_ns, inode->i_gid);
#endif
	meta->u.st.st_rdev = inode->i_rdev;
	meta->u.st.st_size = i_size_read(inode);
	meta->u.st.st_blocks = lightfs_get_block_num_by_size(meta->u.st.st_size);
	meta->u.st.st_blksize = LIGHTFS_BSTORE_BLOCKSIZE;
	TIMESPEC_TO_TIME_T(meta->u.st.st_atime, inode->i_atime);
	TIMESPEC_TO_TIME_T(meta->u.st.st_mtime, inode->i_mtime);
	TIMESPEC_TO_TIME_T(meta->u.st.st_ctime, inode->i_ctime);
}

#ifndef SUPER_NOLOCK
static inline DBT *lightfs_get_read_lock(struct lightfs_inode *f_inode)
{
	down_read(&f_inode->key_lock);
	return &f_inode->meta_dbt;
}

static inline void lightfs_put_read_lock(struct lightfs_inode *f_inode)
{
	up_read(&f_inode->key_lock);
}

static inline DBT *lightfs_get_write_lock(struct lightfs_inode *f_inode)
{
	down_write(&f_inode->key_lock);
	return &f_inode->meta_dbt;
}

static inline void lightfs_put_write_lock(struct lightfs_inode *f_inode)
{
	up_write(&f_inode->key_lock);
}
#else
static inline DBT *lightfs_get_read_lock(struct lightfs_inode *f_inode)
{
	return &f_inode->meta_dbt;
}

static inline void lightfs_put_read_lock(struct lightfs_inode *f_inode)
{
}

static inline DBT *lightfs_get_write_lock(struct lightfs_inode *f_inode)
{
	return &f_inode->meta_dbt;
}

static inline void lightfs_put_write_lock(struct lightfs_inode *f_inode)
{
}
#endif


// get the next available (unused ino)
// we alloc some ino to each cpu, if more are needed, we will do update_ino
static int lightfs_next_ino(struct lightfs_sb_info *sbi, ino_t *ino)
{
	int ret = 0;
	unsigned int cpu;
	ino_t new_max;
	DB_TXN *txn;

	new_max = 0;
	cpu = get_cpu();
	*ino = per_cpu_ptr(sbi->s_lightfs_info, cpu)->next_ino;
	if (*ino >= per_cpu_ptr(sbi->s_lightfs_info, cpu)->max_ino) {
		// we expand for all cpus here, it is lavish
		// we can't do txn while holding cpu
		new_max = per_cpu_ptr(sbi->s_lightfs_info, cpu)->max_ino +
		          sbi->s_nr_cpus * LIGHTFS_INO_INC;
		per_cpu_ptr(sbi->s_lightfs_info, cpu)->max_ino = new_max;
	}
	per_cpu_ptr(sbi->s_lightfs_info, cpu)->next_ino += sbi->s_nr_cpus;
	put_cpu();

	if (new_max) {
		TXN_GOTO_LABEL(retry);
		lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
		ret = lightfs_bstore_update_ino(sbi->meta_db, txn, new_max);
		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
			// we already updated max_cpu, if we get error here
			//  it is hard to go back
			BUG();
		}
		ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
		COMMIT_JUMP_ON_CONFLICT(ret, retry);
	}

	return ret;
}

void copy_meta_dbt_from_ino(DBT *dbt, uint64_t ino)
{
	char *meta_key = dbt->data;
	size_t size;

	size = SIZEOF_ROOT_META_KEY;
	BUG_ON(size > dbt->ulen);
	lightfs_key_set_magic(meta_key, META_KEY_MAGIC);
	lightfs_key_set_ino(meta_key, ino);
	(lightfs_key_path(meta_key))[0] = '\0';

	dbt->size = size;
}

void
copy_data_dbt_from_meta_dbt(DBT *data_dbt, DBT *meta_dbt, uint64_t block_num)
{
	char *meta_key = meta_dbt->data;
	char *data_key = data_dbt->data;
	size_t size;

	size = meta_dbt->size + DATA_META_KEY_SIZE_DIFF;
	BUG_ON(size > data_dbt->ulen);
	lightfs_key_set_magic(data_key, DATA_KEY_MAGIC);
	lightfs_key_copy_ino(data_key, meta_key);
	strcpy(lightfs_key_path(data_key), lightfs_key_path(meta_key));
	lightfs_data_key_set_blocknum(data_key, size, block_num);

	data_dbt->size = size;
}

int
alloc_data_dbt_from_meta_dbt(DBT *data_dbt, DBT *meta_dbt, uint64_t block_num)
{
	char *meta_key = meta_dbt->data;
	char *data_key;
	size_t size;

	size = meta_dbt->size + DATA_META_KEY_SIZE_DIFF;
	data_key = kmalloc(size, GFP_NOIO);
	if (data_key == NULL)
		return -ENOMEM;
	lightfs_key_set_magic(data_key, DATA_KEY_MAGIC);
	lightfs_key_copy_ino(data_key, meta_key);
	strcpy(lightfs_key_path(data_key), lightfs_key_path(meta_key));
	lightfs_data_key_set_blocknum(data_key, size, block_num);

	dbt_setup(data_dbt, data_key, size);
	return 0;
}

int
alloc_child_meta_dbt_from_meta_dbt(DBT *dbt, DBT *parent_dbt, const char *name)
{
	char *parent_key = parent_dbt->data;
	char *meta_key;
	size_t size;
	char *last_slash;

	if ((lightfs_key_path(parent_key))[0] == '\0')
		size = parent_dbt->size + strlen(name) + 2;
	else
		size = parent_dbt->size + strlen(name) + 1;
	meta_key = kmalloc(size, GFP_NOIO);
	if (meta_key == NULL)
		return -ENOMEM;
	lightfs_key_set_magic(meta_key, META_KEY_MAGIC);
	lightfs_key_copy_ino(meta_key, parent_key);
	if ((lightfs_key_path(parent_key))[0] == '\0') {
		sprintf(lightfs_key_path(meta_key), "\x01\x01%s", name);
	} else {
		last_slash = strrchr(lightfs_key_path(parent_key), '\x01');
		BUG_ON(last_slash == NULL);
		memcpy(lightfs_key_path(meta_key), lightfs_key_path(parent_key),
		       last_slash - lightfs_key_path(parent_key));
		sprintf(lightfs_key_path(meta_key) + (last_slash - lightfs_key_path(parent_key)),
		        "%s\x01\x01%s", last_slash + 1, name);
	}

	dbt_setup(dbt, meta_key, size);
	return 0;
}

void
copy_data_dbt_from_inode(DBT *data_dbt, struct inode *inode, uint64_t block_num)
{
	char *data_key = data_dbt->data;
	size_t size;
	uint64_t ino = inode->i_ino;

	size = PATH_POS + DATA_META_KEY_SIZE_DIFF;
	BUG_ON(size > data_dbt->ulen);
	lightfs_key_set_magic(data_key, DATA_KEY_MAGIC);
	lightfs_key_set_ino(data_key, ino);
	lightfs_data_key_set_blocknum(data_key, size, block_num);

	data_dbt->size = size;
}

int
alloc_data_dbt_from_inode(DBT *data_dbt, struct inode *inode, uint64_t block_num)
{
	char *data_key;
	size_t size;
	uint64_t ino = inode->i_ino;

	size = PATH_POS + DATA_META_KEY_SIZE_DIFF;
	data_key = kmalloc(size, GFP_NOIO);
	if (data_key == NULL)
		return -ENOMEM;
	lightfs_key_set_magic(data_key, DATA_KEY_MAGIC);
	lightfs_key_set_ino(data_key, ino);
	lightfs_data_key_set_blocknum(data_key, size, block_num);

	dbt_setup(data_dbt, data_key, size);
	return 0;
}

int
alloc_data_dbt_from_ino(DBT *data_dbt, uint64_t ino, uint64_t block_num)
{
	char *data_key;
	size_t size;

	size = PATH_POS + DATA_META_KEY_SIZE_DIFF;
	data_key = kmalloc(size, GFP_NOIO);
	if (data_key == NULL)
		return -ENOMEM;
	lightfs_key_set_magic(data_key, DATA_KEY_MAGIC);
	lightfs_key_set_ino(data_key, ino);
	lightfs_data_key_set_blocknum(data_key, size, block_num);

	dbt_setup(data_dbt, data_key, size);
	return 0;
}


int
alloc_child_meta_dbt_from_inode(DBT *dbt, struct inode *dir, const char *name)
{
	char *meta_key;
	size_t size;
	uint64_t parent_ino = dir->i_ino;

	size = PATH_POS + strlen(name) + 1;
	meta_key = kmalloc(size, GFP_NOIO);
	if (meta_key == NULL)
		return -ENOMEM;
	lightfs_key_set_magic(meta_key, META_KEY_MAGIC);
	lightfs_key_set_ino(meta_key, parent_ino);
	sprintf(lightfs_key_path(meta_key), "%s", name);

	dbt_setup(dbt, meta_key, size);
	return 0;
}

//TODO: KOO fix it.
int alloc_meta_dbt_prefix(DBT *prefix_dbt, DBT *meta_dbt)
{
	char *meta_key = meta_dbt->data;
	char *prefix_key;
	size_t size;
	char *last_slash;

	if ((lightfs_key_path(meta_key))[0] == '\0')
		size = meta_dbt->size;
	else
		size = meta_dbt->size - 1;
	prefix_key = kmalloc(size, GFP_NOIO);
	if (prefix_key == NULL)
		return -ENOMEM;
	lightfs_key_set_magic(prefix_key, META_KEY_MAGIC);
	lightfs_key_copy_ino(prefix_key, meta_key);
	if ((lightfs_key_path(meta_key))[0] == '\0') {
		(lightfs_key_path(prefix_key))[0] = '\0';
	} else {
		last_slash = strrchr(lightfs_key_path(meta_key), '\x01');
		BUG_ON(last_slash == NULL);
		memcpy(lightfs_key_path(prefix_key), lightfs_key_path(meta_key),
		       last_slash - lightfs_key_path(meta_key));
		strcpy(lightfs_key_path(prefix_key) + (last_slash - lightfs_key_path(meta_key)),
		       last_slash + 1);
	}

	dbt_setup(prefix_dbt, prefix_key, size);
	return 0;
}

static struct inode *
lightfs_setup_inode(struct super_block *sb, DBT *meta_dbt,
                 struct lightfs_metadata *meta);

static int
lightfs_do_unlink(DBT *meta_dbt, DB_TXN *txn, struct inode *inode,
               struct lightfs_sb_info *sbi)
{
	int ret;

	ret = lightfs_bstore_meta_del(sbi->meta_db, meta_dbt, txn, 0, S_ISDIR(inode->i_mode));

	if (S_ISDIR(inode->i_mode))
		return 0;

	if (!ret && i_size_read(inode) > 0)
		ret = lightfs_bstore_trunc(sbi->data_db, meta_dbt, txn, 0, 0, inode);

	return ret;
}

static inline int meta_key_is_root(char *meta_key)
{
	return 1;
	return ((lightfs_key_path(meta_key))[0] == '\0');
}


static int lightfs_readpage(struct file *file, struct page *page)
{
	int ret;
	struct inode *inode = page->mapping->host;
	struct lightfs_sb_info *sbi = inode->i_sb->s_fs_info;
	DBT *meta_dbt;
	DB_TXN *txn;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));

	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_READONLY);
	ret = lightfs_bstore_scan_one_page(sbi->data_db, meta_dbt, txn, page, inode);
	if (ret) {
		DBOP_JUMP_ON_CONFLICT(ret, retry);
		lightfs_bstore_txn_abort(txn);
	} else {
		ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
		COMMIT_JUMP_ON_CONFLICT(ret, retry);
	}

	lightfs_put_read_lock(LIGHTFS_I(inode));

	flush_dcache_page(page);
	if (!ret) {
		SetPageUptodate(page);
	} else {
		BUG_ON(1);
		ClearPageUptodate(page);
		SetPageError(page);
	}

	unlock_page(page); //TMP 

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	BUG_ON(ret);
	return ret;
}

static int lightfs_readpages(struct file *filp, struct address_space *mapping,
                          struct list_head *pages, unsigned nr_pages)
{
	int ret = 0;
	struct lightfs_sb_info *sbi = mapping->host->i_sb->s_fs_info;
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(mapping->host);
	struct lightfs_io *lightfs_io;
	DBT *meta_dbt = NULL;
	DB_TXN *txn;
	struct inode *inode = mapping->host;
	unsigned buffer_processed_pages = 0, free_ra_entry_num = 0;
	volatile bool fg_read = true;
	struct reada_entry *ra_entry;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif


#ifdef CALL_TRACE
	lightfs_error(__func__, "path: %s\n", filp->f_path.dentry->d_name.name);
#endif


	lightfs_io = lightfs_io_alloc(nr_pages);
	if (!lightfs_io) {
		return -ENOMEM;
	}
	lightfs_io_setup(lightfs_io, pages, nr_pages, mapping);

	/* 
	 * step1: search the read ahead buffer of issue foreground read
	 *   case 1: buffer found. ==> service the request
	 *   case 2: buffer found but partialy ==> issue foreground read 
	 *   case 3: buffer not found
	*      case 3-1: read ahead is already issued ==> wait the issued read
	 *     case 3-2: issue foreground read
	 * step2: decide whether read ahead is needed or not
	 *   condition: if requested number of pages to be read is greater/equal than READA_THRESHOLD and start page number of requests is equal last_request_page_number + 1 then, we issue read ahead
	 */

#ifdef READA
	list_for_each_entry(ra_entry, &lightfs_inode->ra_list, list) {
		ret = buffer_processed_pages = lightfs_reada_buffer_get(ra_entry, inode, lightfs_io, nr_pages);
		buffer_processed_pages += ret;
		if (ret == ENORA) { // no buffer
			//fg_read = true;
			//should_free_ra_entry = false;
			break;
		} else if (ret == ESMALL) { // small request
			//fg_read = true;
			//should_free_ra_entry = true;
			break;
		} else if (ret) { // buffer hit
			if (ret == nr_pages) {
				//fg_read = false;
				//should_free_ra_entry = false;
				break;
			} else if (buffer_processed_pages == nr_pages) {
				//fg_read = false;
				//should_free_ra_entry = true;
				break;
			} else {
				free_ra_entry_num++;
			}
		} else { // buffer miss
			//fg_read = true;
			//should_free_ra_entry = true;
			free_ra_entry_num++;
		}
	}

	if (ret == ESMALL) { // all flush
		//pr_info("ESMALL: %d\n", nr_pages);
		lightfs_reada_all_flush(inode);
		fg_read = true;
	} else if (free_ra_entry_num) { // buffer hit or not
		lightfs_reada_flush(inode, free_ra_entry_num);
		fg_read = true;
	}

	if (buffer_processed_pages == nr_pages) {
		fg_read = false;
	}

	/*
	buffer_processed_pages = lightfs_reada_buffer_get(inode, lightfs_io, nr_pages);

	if (buffer_processed_pages != ENORA) { // buffer exists
		if (buffer_processed_pages) { // buffer hit
			if (buffer_processed_pages == nr_pages) { // case 1
				fg_read = false;
			} else { // case 2
				fg_read = true;		
			}
		} else { // should free reada case 3: buffer miss
			lightfs_reada_free(inode);
			fg_read = true;
		}
	} else { // case 3-2: buffer is not occupied
		fg_read = true;
	}
	*/
#endif

	if (fg_read) {
		meta_dbt = lightfs_get_read_lock(lightfs_inode);

		TXN_GOTO_LABEL(retry);
		lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_READONLY);
	
		ret = lightfs_bstore_scan_pages(sbi->data_db, meta_dbt, txn, lightfs_io, inode);

		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
		} else {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			COMMIT_JUMP_ON_CONFLICT(ret, retry);
		}
	
		lightfs_put_read_lock(lightfs_inode);
	
		if (ret)
			lightfs_io_set_pages_error(lightfs_io);
		else
			lightfs_io_set_pages_uptodate(lightfs_io);
		lightfs_io_unlock_pages(lightfs_io);
	} else {
		lightfs_io_set_pages_uptodate(lightfs_io);
		lightfs_io_unlock_pages(lightfs_io);
	}

#ifdef READA
	if (lightfs_reada_need(inode, lightfs_io, nr_pages, fg_read)) {
		volatile bool is_eof = false;
		unsigned iter = 0, max_reada;
		spin_lock(&lightfs_inode->reada_spin);
		max_reada = READA_QD - lightfs_inode->ra_entry_cnt;
		while (iter < max_reada) {
			TXN_GOTO_LABEL(retry_reada);
			lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_READONLY);

			ret = lightfs_bstore_reada_pages(sbi->data_db, meta_dbt, txn, lightfs_io, inode, iter);

			if (ret && ret != EEOF) {
				DBOP_JUMP_ON_CONFLICT(ret, retry);
				lightfs_bstore_txn_abort(txn);
			} else {
				if (ret == EEOF) {
					is_eof = true;
				}
				ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
				COMMIT_JUMP_ON_CONFLICT(ret, retry_reada);
			}
			if (is_eof) {
				break;
			}
			iter++;
		}
		spin_unlock(&lightfs_inode->reada_spin);
	}
#endif
	lightfs_io_free(lightfs_io);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif

	BUG_ON(ret);

	return ret;
}

static int
__lightfs_writepage(struct lightfs_sb_info *sbi, struct inode *inode, DBT *meta_dbt,
                 struct page *page, size_t len, DB_TXN *txn)
{
	int ret;
	DBT data_dbt;
#ifndef WB
	char *buf;
#endif
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	// now data_db keys start from 1
	// KOO:key
	//ret = alloc_data_dbt_from_meta_dbt(&data_dbt, meta_dbt,
	//                                   PAGE_TO_BLOCK_NUM(page));
	ret = alloc_data_dbt_from_inode(&data_dbt, inode, PAGE_TO_BLOCK_NUM(page));
	if (ret)
		return ret;
#ifndef WB
	buf = kmap_atomic(page); //WBWB
	ret = lightfs_bstore_put(sbi->data_db, &data_dbt, txn, buf, len, 0);
	kunmap_atomic(buf); //WBWB
#else
	ret = lightfs_bstore_put_page(sbi->data_db, &data_dbt, txn, page, len, 0);
#endif
	dbt_destroy(&data_dbt);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif

	return ret;
}

static int
lightfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret;
	DBT *meta_dbt;
	struct inode *inode = page->mapping->host;
	struct lightfs_sb_info *sbi = inode->i_sb->s_fs_info;
	loff_t i_size;
	pgoff_t end_index;
	unsigned offset;
	DB_TXN *txn;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));
	set_page_writeback(page);
	i_size = i_size_read(inode);
	end_index = i_size >> PAGE_SHIFT;

	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
	if (page->index < end_index)
		ret = __lightfs_writepage(sbi, inode, meta_dbt, page, PAGE_SIZE, txn);
	else {
		offset = i_size & (~PAGE_MASK);
		if (page->index == end_index && offset != 0)
			ret = __lightfs_writepage(sbi, inode, meta_dbt, page, offset, txn);
		else
			ret = 0;
	}
	if (ret) {
		DBOP_JUMP_ON_CONFLICT(ret, retry);
		lightfs_bstore_txn_abort(txn);
		if (ret == -EAGAIN) {
			redirty_page_for_writepage(wbc, page);
			ret = 0;
		} else {
			SetPageError(page);
			mapping_set_error(page->mapping, ret);
		}
	} else {
		ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
		COMMIT_JUMP_ON_CONFLICT(ret, retry);
	}
	
	//end_page_writeback(page); //WBWB
#ifndef WB
	end_page_writeback(page); //WBWB
#endif

	lightfs_put_read_lock(LIGHTFS_I(inode));
	unlock_page(page); 

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

struct lightfs_wp_node {
	struct page *page;
	struct lightfs_wp_node *next;
};
#define LIGHTFS_WRITEPAGES_LIST_SIZE 4096

static struct kmem_cache *lightfs_writepages_cachep;

static int
__lightfs_writepages_write_pages(struct lightfs_wp_node *list, int nr_pages,
                              struct writeback_control *wbc,
                              struct inode *inode, struct lightfs_sb_info *sbi,
                              DBT *data_dbt, int is_seq)
{
	int i, ret = 0;
	loff_t i_size;
	pgoff_t end_index;
	unsigned offset;
	struct lightfs_wp_node *it;
	struct page *page;
	DBT *meta_dbt;
	char *data_key;
	DB_TXN *txn = NULL;
#ifndef WB
	char *buf;
#endif
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));
	data_key = data_dbt->data;
	if (unlikely(!key_is_same_of_key((char *)meta_dbt->data, data_key))) // KOO:key: is it necessary?
		copy_data_dbt_from_meta_dbt(data_dbt, meta_dbt, 0);
retry:
	i_size = i_size_read(inode);
	end_index = i_size >> PAGE_SHIFT;
	offset = i_size & (PAGE_SIZE - 1);

	//lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
	// we did a lazy approach about the list, so we need an additional i here
	for (i = 0, it = list->next; i < nr_pages; i++, it = it->next) {
		if (!(i % LIGHTFS_TXN_LIMIT)) {
			lightfs_bstore_txn_begin(sbi->db_dev, NULL, &txn, TXN_MAY_WRITE);
		}
		page = it->page;
		lightfs_data_key_set_blocknum(data_key, data_dbt->size,
		                           PAGE_TO_BLOCK_NUM(page));
#ifndef WB
		buf = kmap_atomic(page); //WBWB
		if (page->index < end_index)
			ret = lightfs_bstore_put(sbi->data_db, data_dbt, txn, buf,
			                      PAGE_SIZE, is_seq); //WBWB

		else if (page->index == end_index && offset != 0)
			ret = lightfs_bstore_put(sbi->data_db, data_dbt, txn, buf,
			                      offset, is_seq); //WBWB
		else
			ret = 0;
		kunmap_atomic(buf); //WBWB
#else
		if (page->index < end_index)
			ret = lightfs_bstore_put_page(sbi->data_db, data_dbt, txn, page, PAGE_SIZE, is_seq);

		else if (page->index == end_index && offset != 0) {
			zero_user_segment(page, offset, PAGE_SIZE);
			//char *buf = kmap(page);
			//memset(buf + offset, 0, PAGE_SIZE - offset);
			//kunmap(page);
			ret = lightfs_bstore_put_page(sbi->data_db, data_dbt, txn, page, offset, is_seq);
		} else
			ret = 0;
#endif

		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
			goto out;
		}
		if ((i % LIGHTFS_TXN_LIMIT) == LIGHTFS_TXN_LIMIT-1) {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			COMMIT_JUMP_ON_CONFLICT(ret, retry);
			txn = NULL;
		}

	}
	if (txn) {
		ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
		COMMIT_JUMP_ON_CONFLICT(ret, retry);
	}
out:
	lightfs_put_read_lock(LIGHTFS_I(inode));
	for (i = 0, it = list->next; i < nr_pages; i++, it = it->next) {
		page = it->page;
		//end_page_writeback(page); //WBWB
#ifndef WB
		end_page_writeback(page); //WBWB
#endif
		if (ret)
			redirty_page_for_writepage(wbc, page);
		unlock_page(page); //TMP
	}
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

/**
 * (mostly) copied from write_cache_pages
 *
 * however, instead of calling mm/page-writeback.c:__writepage, we
 * detect large I/Os and potentially issue a special seq_put to our
 * B^e tree
 */
static int lightfs_writepages(struct address_space *mapping,
			struct writeback_control *wbc)
{
	int ret = 0;
	int done = 0;
	struct pagevec pvec;
	int nr_pages;
	pgoff_t uninitialized_var(writeback_index);
	pgoff_t index;
	pgoff_t end;		/* Inclusive */
	pgoff_t done_index, txn_done_index;
	int cycled;
	int range_whole = 0;
	int tag;
	//int is_seq = 0;
	struct inode *inode;
	struct lightfs_sb_info *sbi;
	DBT *meta_dbt, data_dbt;
	int nr_list_pages;
	struct lightfs_wp_node list, *tail, *it;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	pagevec_init(&pvec);
	if (wbc->range_cyclic) {
		writeback_index = mapping->writeback_index; /* prev offset */
		index = writeback_index;
		if (index == 0)
			cycled = 1;
		else
			cycled = 0;
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_SHIFT;
		end = wbc->range_end >> PAGE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		cycled = 1; /* ignore range_cyclic tests */
	}
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, index, end);
	done_index = index;
	txn_done_index = index;

	inode = mapping->host;
	sbi = inode->i_sb->s_fs_info;
	meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));
	ret = dbt_alloc(&data_dbt, DATA_KEY_MAX_LEN);
	if (ret) {
		lightfs_put_read_lock(LIGHTFS_I(inode));
		goto out;
	}
	//copy_data_dbt_from_meta_dbt(&data_dbt, meta_dbt, 0); //TODO:key
	copy_data_dbt_from_inode(&data_dbt, inode, 0);
	lightfs_put_read_lock(LIGHTFS_I(inode));

	nr_list_pages = 0;
	list.next = NULL;
	tail = &list;
	while (!done && (index <= end)) {
		int i;
		nr_pages = pagevec_lookup_range_tag(&pvec, mapping, &index, end, tag);
		if (nr_pages == 0)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			if (page->index > end) {
				done = 1;
				break;
			}

			txn_done_index = page->index;
			lock_page(page); 

			if (unlikely(page->mapping != mapping)) {
continue_unlock:
				unlock_page(page); 
				continue;
			}

			if (!PageDirty(page)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (PageWriteback(page)) {
				if (wbc->sync_mode != WB_SYNC_NONE) {
					wait_on_page_writeback(page);
				} else {
					goto continue_unlock;
				}
			}

			BUG_ON(PageWriteback(page));
			if (!clear_page_dirty_for_io(page))
				goto continue_unlock;

			set_page_writeback(page); //asd
			if (tail->next == NULL) {
				tail->next = kmem_cache_alloc(
					lightfs_writepages_cachep, GFP_KERNEL);
				tail->next->next = NULL;
			}
			tail = tail->next;
			tail->page = page;
			++nr_list_pages;
			if (nr_list_pages >= LIGHTFS_WRITEPAGES_LIST_SIZE) {
				ret = __lightfs_writepages_write_pages(&list,
					nr_list_pages, wbc, inode, sbi,
					&data_dbt, 0);
				if (ret)
					goto free_dkey_out;
				done_index = txn_done_index;
				nr_list_pages = 0;
				tail = &list;
			}

			if (--wbc->nr_to_write <= 0 &&
			    wbc->sync_mode == WB_SYNC_NONE) {
				done = 1;
				break;
			}
		}
		pagevec_release(&pvec);
		cond_resched();
	}

	if (nr_list_pages > 0) {
		ret = __lightfs_writepages_write_pages(&list, nr_list_pages, wbc,
			inode, sbi, &data_dbt, 0);
		if (!ret)
			done_index = txn_done_index;
	}
free_dkey_out:
	dbt_destroy(&data_dbt);
	tail = list.next;
	while (tail != NULL) {
		it = tail->next;
		kmem_cache_free(lightfs_writepages_cachep, tail);
		tail = it;
	}
out:
	if (!cycled && !done) {
		cycled = 1;
		index = 0;
		end = writeback_index - 1;
		goto retry;
	}
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = done_index;

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

static int
lightfs_write_begin(struct file *file, struct address_space *mapping,
                 loff_t pos, unsigned len, unsigned flags,
                 struct page **pagep, void **fsdata)
{
	int ret = 0;
	struct page *page;
	struct inode *inode = mapping->host;
	pgoff_t index = pos >> PAGE_SHIFT;
	unsigned from, to;
	struct lightfs_sb_info *sbi = inode->i_sb->s_fs_info;
	DBT *meta_dbt;
	DB_TXN *txn;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

	from = pos & (PAGE_SIZE -1);
	to = from + len;
#ifdef CALL_TRACE
	//lightfs_error(__func__, "\n");
#endif

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		ret = -ENOMEM;
	}

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	*pagep = page;

	if (len == PAGE_SIZE || PageUptodate(page)) {
		ret = 0;
		goto out;
	}

	if (!(pos & (PAGE_SIZE - 1)) && (pos + len) >= i_size_read(inode)) {
		zero_user_segment(page, 0, PAGE_SIZE);
		/*
		char *buf;
		buf = kmap(page);
		memset(buf, 0, PAGE_SIZE);
		kunmap(page);
		*/
		ret = 0;
		goto out;
	}


	if (!PageDirty(page)) {
		meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));
		TXN_GOTO_LABEL(retry);
		//pr_info ("pos: %llu, len: %u, from: %u, to: %u, file_size: %llu\n", pos, len, from, to, i_size_read(inode));
	
		lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_READONLY);
		ret = lightfs_bstore_scan_one_page(sbi->data_db, meta_dbt, txn, page, inode);
		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
		} else {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			COMMIT_JUMP_ON_CONFLICT(ret, retry);
		}
		lightfs_put_read_lock(LIGHTFS_I(inode));
		BUG_ON(ret);
	}

#if 0
	if (!PageDirty(page) && pos + len <= i_size_read(inode)) {
		if (to != PAGE_SIZE || from) {
			meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));
			TXN_GOTO_LABEL(retry);
		
			//pr_info ("pos: %llu, len: %llu, from: %llu, to: %llu, file_size: %llu\n", pos, len, from, to, i_size_read(inode));
		
			lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_READONLY);
			ret = lightfs_bstore_scan_one_page(sbi->data_db, meta_dbt, txn, page, inode);
			if (ret) {
				DBOP_JUMP_ON_CONFLICT(ret, retry);
				lightfs_bstore_txn_abort(txn);
			} else {
				ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
				COMMIT_JUMP_ON_CONFLICT(ret, retry);
			}
			lightfs_put_read_lock(LIGHTFS_I(inode));
			BUG_ON(ret);
		}
	} else if (page_offset(page) >= i_size_read(inode)) {
		char *buf = kmap(page);
		pr_info("need memset\n");
		if (from) {
			memset(buf, 0, from);
		}
		if (to != PAGE_SIZE) {
			memset(buf + to, 0, PAGE_SIZE - to);
		}
		kunmap(page);
	}
#endif
	/* don't read page if not uptodate */
	ret = 0;
out:

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

static int
lightfs_write_end(struct file *file, struct address_space *mapping,
               loff_t pos, unsigned len, unsigned copied,
               struct page *page, void *fsdata)
{
	/* make sure that lightfs can't guarantee uptodate page */
	loff_t last_pos = pos + copied;
	struct inode *inode = page->mapping->host;
	loff_t old_size = inode->i_size;
	bool i_size_changed = 0;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif


#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	if (!PageUptodate(page)) {
		if (copied < len) {
			lightfs_error(__func__, "copy!!!\n");
			copied = 0;
		}
		SetPageUptodate(page);
	}
	if (!PageDirty(page))
		__set_page_dirty_nobuffers(page); //asd
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif


	if (last_pos > inode->i_size) {
		i_size_write(inode, last_pos);
		i_size_changed = 1;
	}


#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif
	if (old_size < pos)
		pagecache_isize_extended(inode, old_size, pos);

	if (i_size_changed) {
		mark_inode_dirty(inode);
	}
	unlock_page(page);
	put_page(page);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return copied;
}

/* Called before freeing a page - it writes back the dirty page.
 *
 * To prevent redirtying the page, it is kept locked during the whole
 * operation.
 */
static int lightfs_launder_page(struct page *page)
{
	printk(KERN_CRIT "laundering page.\n");
	BUG();
}

static int lightfs_rename(struct inode *old_dir, struct dentry *old_dentry,
                       struct inode *new_dir, struct dentry *new_dentry,
					   unsigned int flags)
{
	int ret, err;
	struct inode *old_inode, *new_inode;
	struct lightfs_sb_info *sbi = old_dir->i_sb->s_fs_info;
	DBT *old_meta_dbt, new_meta_dbt, *old_dir_meta_dbt, *new_dir_meta_dbt,
	    *new_inode_meta_dbt;
	struct lightfs_metadata old_meta;
	DB_TXN *txn;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif


	// to prevent any other move from happening, we grab sem of parents
	old_dir_meta_dbt = lightfs_get_read_lock(LIGHTFS_I(old_dir));
	new_dir_meta_dbt = lightfs_get_read_lock(LIGHTFS_I(new_dir));

	old_inode = old_dentry->d_inode;
	old_meta_dbt = lightfs_get_write_lock(LIGHTFS_I(old_inode));
	new_inode = new_dentry->d_inode;
	new_inode_meta_dbt = new_inode ?
	lightfs_get_write_lock(LIGHTFS_I(new_inode)) : NULL;
	//prelock_children_for_rename(old_dentry, &locked_children);

	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);

	if (flags & RENAME_WHITEOUT) {
		ret = -ENOENT;
	lightfs_error(__func__, "WHITEOUT\n");
		goto abort;
	}

	if (new_inode) { // New file already exists
		if (S_ISDIR(old_inode->i_mode)) { // and it is a directory
			if (!S_ISDIR(new_inode->i_mode)) { // and it is a file
				ret = -ENOTDIR;
				goto abort;
			}
			err = lightfs_dir_is_empty(sbi->meta_db, new_inode_meta_dbt,
			                        txn, &ret, new_inode);
			if (err) {
				DBOP_JUMP_ON_CONFLICT(err, retry);
				ret = err;
				goto abort;
			}
			if (!ret) {
				ret = -ENOTEMPTY;
				goto abort;
			}
		} else { // old is a regular file
			if (S_ISDIR(new_inode->i_mode)) {
				ret = -ENOTDIR;
				goto abort;
			}
		}
	}

	//KOO:key
	ret = alloc_child_meta_dbt_from_inode(&new_meta_dbt, new_dir, new_dentry->d_name.name);
	if (ret)
		goto abort;

	lightfs_copy_metadata_from_inode(&old_meta, old_inode);
	ret = lightfs_bstore_meta_del(sbi->meta_db, old_meta_dbt, txn, 0, false);
	if (!ret)
		ret = lightfs_bstore_meta_put(sbi->meta_db, &new_meta_dbt, txn, &old_meta, new_dir, false);
		//print_key(__func__, old_meta_dbt->data, old_meta_dbt->size);
		//print_key(__func__, new_meta_dbt.data, new_meta_dbt.size);

	if (ret) {
		DBOP_JUMP_ON_CONFLICT(ret, retry);
		goto abort1;
	}

	ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
	COMMIT_JUMP_ON_CONFLICT(ret, retry);

	dbt_destroy(old_meta_dbt);
	dbt_copy(old_meta_dbt, &new_meta_dbt);

	if (new_inode) {
		drop_nlink(new_inode);
		mark_inode_dirty(new_inode);
		// avoid future updates from write_inode and evict_inode
		LIGHTFS_I(new_inode)->lightfs_flags |= LIGHTFS_FLAG_DELETED;
		lightfs_put_write_lock(LIGHTFS_I(new_inode));
	}
	lightfs_put_write_lock(LIGHTFS_I(old_inode));
	lightfs_put_read_lock(LIGHTFS_I(old_dir));
	lightfs_put_read_lock(LIGHTFS_I(new_dir));


#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return 0;

abort1:
	dbt_destroy(&new_meta_dbt);
abort:
	lightfs_bstore_txn_abort(txn);
	//unlock_children_after_rename(&locked_children);
	lightfs_put_write_lock(LIGHTFS_I(old_inode));
	if (new_inode)
		lightfs_put_write_lock(LIGHTFS_I(new_inode));
	lightfs_put_read_lock(LIGHTFS_I(old_dir));
	lightfs_put_read_lock(LIGHTFS_I(new_dir));

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

/*
 * lightfs_readdir: ctx->pos (vfs get from f_pos)
 *   ctx->pos == 0, readdir just starts
 *   ctx->pos == 1/2, readdir has emit dots, used by dir_emit_dots
 *   ctx->pos == 3, readdir has emit all entries
 *   ctx->pos == ?, ctx->pos stores a pointer to the position of last readdir
 */

static int lightfs_readdir(struct file *file, struct dir_context *ctx)
{
	int ret;
	struct inode *inode = file_inode(file);
	struct lightfs_sb_info *sbi = inode->i_sb->s_fs_info;
	DBT *meta_dbt = &(LIGHTFS_I(inode)->meta_dbt);
	DB_TXN *txn;
	struct readdir_ctx *dir_ctx;
	DBC *cursor;
#ifdef DISABLE_DCACHE
	DBC *dcursor;
#endif
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	if (ctx->pos == 0 || ctx->pos == 1) {
		if(!dir_emit_dots(file, ctx))
			return -ENOMEM;
		ctx->pos = 2;
	}

	if (ctx->pos == 2) {
		dir_ctx = kmalloc(sizeof(struct readdir_ctx), GFP_NOIO); 
		ret = sbi->cache_db->cursor(sbi->cache_db, txn, &cursor, LIGHTFS_META_CURSOR);
		if (ret) {
			kfree(dir_ctx);
			return 0;
		}
		dir_ctx->cursor = cursor;
		dir_ctx->emit_cnt = 0;
#ifdef DISABLE_DCACHE
		lightfs_bstore_txn_begin(dbi->db_env, NULL, &txn, TXN_READONLY);
		ret = sbi->meta_db->cursor(sbi->meta_db, txn, &dcursor, LIGHTFS_META_CURSOR);
		dir_ctx->dcursor = dcursor;
		if (ret) {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			kfree(dir_ctx);
			return 0;
		}
		dir_ctx->txn = txn;
#endif
		//lightfs_error(__func__, "dcace alloc!!!! dir_ctx: %px, dir_ctx->pos: %d, dir->cursor: %p\n", dir_ctx, dir_ctx->pos, dir_ctx->cursor);
	} else {
		dir_ctx = (struct readdir_ctx *)(ctx->pos);
		if (dir_ctx->pos == 0) { // all cache hit
			//lightfs_error(__func__, "dcache free!!!! dir_ctx: %px, dir_ctx->pos: %d, dir->cursor: %p, emit_cnt: %d\n", dir_ctx, dir_ctx->pos, dir_ctx->cursor, dir_ctx->emit_cnt);
			dir_ctx->cursor->c_close(dir_ctx->cursor);
#ifdef DISABLE_DCACHE
			dir_ctx->dcursor->c_close(dir_ctx->dcursor);
			lightfs_bstore_txn_commit(dir_ctx->txn, DB_TXN_NOSYNC);
#endif
			kfree(dir_ctx);
			ctx->pos = 3;
			return 0;
		} else if (dir_ctx->pos == 1 || dir_ctx->pos == 2) { // 1: partially cache hit 2: cache miss
			dir_ctx->cursor->c_close(dir_ctx->cursor); // close dcache cursor
			//lightfs_error(__func__, "iter free!!!! dir_ctx: %px, dir_ctx->pos: %d, dir->cursor: %px, dir->emit_cnt: %d\n", dir_ctx, dir_ctx->pos, dir_ctx->cursor, dir_ctx->emit_cnt);

			lightfs_bstore_txn_begin(dbi->db_env, NULL, &txn, TXN_READONLY);
			ret = sbi->meta_db->cursor(sbi->meta_db, txn, &cursor, LIGHTFS_META_CURSOR);
			if (ret) {
				ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
				kfree(dir_ctx);
				return 0;
			}
			dir_ctx->cursor = cursor;
			dir_ctx->txn = txn;
			dir_ctx->emit_cnt = 0;
			//lightfs_error(__func__, "iter alloc!!!! dir_ctx: %px, dir_ctx->pos: %d, dir->cursor: %px, dir->emit_cnt: %d\n", dir_ctx, dir_ctx->pos, dir_ctx->cursor, dir_ctx->emit_cnt);
		} else if (dir_ctx->pos == 3) { // end readdir
			//lightfs_error(__func__, "free!!!! dir_ctx: %px, dir_ctx->pos: %d, dir->cursor: %px, dir->emit_cnt: %d\n", dir_ctx, dir_ctx->pos, dir_ctx->cursor, dir_ctx->emit_cnt);
#ifdef DISABLE_DCACHE
			dir_ctx->dcursor->c_close(dir_ctx->dcursor);
#endif
			dir_ctx->cursor->c_close(dir_ctx->cursor);
			lightfs_bstore_txn_commit(dir_ctx->txn, DB_TXN_NOSYNC);
			kfree(dir_ctx);
			ctx->pos = 3;
			return 0;
		}
	}

	ret = lightfs_bstore_meta_readdir(sbi->meta_db, meta_dbt, txn, ctx, inode, dir_ctx);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

static int
lightfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret;
	struct lightfs_sb_info *sbi = file_inode(file)->i_sb->s_fs_info;
	struct inode *inode = file_inode(file);
	DBT *meta_dbt;
	struct lightfs_metadata meta;
	DB_TXN *txn;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "path: %s\n", file->f_path.dentry->d_name.name);
#endif

	//ret = generic_file_fsync(file, start, end, datasync);
	ret = filemap_fdatawrite_range(file->f_mapping, start, end);

	if (!ret && !datasync) {
		meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));
		lightfs_copy_metadata_from_inode(&meta, inode);
		TXN_GOTO_LABEL(retry);
#ifdef GROUP_COMMIT
		lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_SYNC_WRITE);
		//lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
		ret = lightfs_bstore_meta_put(sbi->meta_db, meta_dbt, txn, &meta, inode, false);
#else
		lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
		ret = lightfs_bstore_meta_sync_put(sbi->meta_db, meta_dbt, txn, &meta, inode, false);
#endif
		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
		} else {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			COMMIT_JUMP_ON_CONFLICT(ret, retry);
		}
		lightfs_put_read_lock(LIGHTFS_I(inode));
	}

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

static int
lightfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	int ret;
	struct inode *inode = NULL;
	struct lightfs_metadata meta;
	struct lightfs_sb_info *sbi = dir->i_sb->s_fs_info;
	loff_t dir_size;
	DBT *dir_meta_dbt, meta_dbt;
	ino_t ino;
	DB_TXN *txn;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "path: %s\n", dentry->d_name.name);
#endif

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	dir_meta_dbt = lightfs_get_read_lock(LIGHTFS_I(dir));
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	ret = alloc_child_meta_dbt_from_inode(&meta_dbt, dir, dentry->d_name.name);
	if (ret) {
		goto out;
	}
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	ret = lightfs_next_ino(sbi, &ino);
	if (ret) {
err_free_dbt:
		dbt_destroy(&meta_dbt);
		goto out;
	}
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	dir_size = i_size_read(dir);

	lightfs_setup_metadata(&meta, mode, 0, rdev, ino);
	inode = lightfs_setup_inode(dir->i_sb, &meta_dbt, &meta);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto err_free_dbt;
	}
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	ret = lightfs_bstore_meta_put(sbi->meta_db, &meta_dbt, txn, &meta, dir, mode & S_IFDIR);
	if (ret) {
		DBOP_JUMP_ON_CONFLICT(ret, retry);
		lightfs_bstore_txn_abort(txn);
		set_nlink(inode, 0);
		LIGHTFS_I(inode)->lightfs_flags |= LIGHTFS_FLAG_DELETED;
		dbt_destroy(&LIGHTFS_I(inode)->meta_dbt);
		iput(inode);
		goto out;
	}
	ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
	COMMIT_JUMP_ON_CONFLICT(ret, retry);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	d_instantiate(dentry, inode);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif
	i_size_write(dir, dir_size + 1);
	mark_inode_dirty(dir);


out:
	lightfs_put_read_lock(LIGHTFS_I(dir));
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif



	return ret;
}

static int
lightfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	return lightfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int lightfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	return lightfs_mknod(dir, dentry, mode | S_IFDIR, 0);
}

static int lightfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int ret;
	struct inode *inode = dentry->d_inode;
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
	DBT *meta_dbt;
	loff_t dir_size;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	meta_dbt = lightfs_get_read_lock(lightfs_inode);

	if (meta_dbt->data == &root_meta_key) {
		ret = -EINVAL;
		goto out;
	}

	dir_size = i_size_read(inode);

	clear_nlink(inode);
	mark_inode_dirty(inode);
	ret = 0;

out:
	lightfs_put_read_lock(lightfs_inode);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

static int
lightfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	int ret;
	struct inode *inode;
	struct lightfs_sb_info *sbi = dir->i_sb->s_fs_info;
	struct lightfs_metadata meta;
	DBT *dir_meta_dbt, meta_dbt;
	DBT data_dbt;
	size_t len = strlen(symname);
	ino_t ino;
	DB_TXN *txn;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	if (len > LIGHTFS_BSTORE_BLOCKSIZE)
		return -ENAMETOOLONG;

	dir_meta_dbt = lightfs_get_read_lock(LIGHTFS_I(dir));
	ret = alloc_child_meta_dbt_from_inode(&meta_dbt, dir, dentry->d_name.name);
	if (ret)
		goto out;

	ret = lightfs_next_ino(sbi, &ino);
	if (ret) {
free_meta_out:
		dbt_destroy(&meta_dbt);
		goto out;
	}

	ret = alloc_data_dbt_from_ino(&data_dbt, ino, 1);
	if (ret) {
		goto free_meta_out;
	}

	lightfs_setup_metadata(&meta, S_IFLNK | S_IRWXUGO, len, 0, ino);
	inode = lightfs_setup_inode(dir->i_sb, &meta_dbt, &meta);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		dbt_destroy(&data_dbt);
		goto free_meta_out;
	}

	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_SYNC_WRITE);
	ret = lightfs_bstore_meta_put(sbi->meta_db, &meta_dbt, txn, &meta, dir, false);
	if (ret) {
abort:
		DBOP_JUMP_ON_CONFLICT(ret, retry);
		lightfs_bstore_txn_abort(txn);
		set_nlink(inode, 0);
		LIGHTFS_I(inode)->lightfs_flags |= LIGHTFS_FLAG_DELETED;
		dbt_destroy(&LIGHTFS_I(inode)->meta_dbt);
		iput(inode);
		dbt_destroy(&data_dbt);
		goto out;
	}
	ret = lightfs_bstore_put(sbi->data_db, &data_dbt, txn, symname, len, 0);
	//print_key(__func__, meta_dbt.data, meta_dbt.size);	
	//print_key(__func__, data_dbt.data, data_dbt.size);	
	if (ret)
		goto abort;

	ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
	COMMIT_JUMP_ON_CONFLICT(ret, retry);

	d_instantiate(dentry, inode);
	dbt_destroy(&data_dbt);
out:
	lightfs_put_read_lock(LIGHTFS_I(dir));

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

static int lightfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int ret = 0;
	struct inode *inode = dentry->d_inode;
	DBT *dir_meta_dbt, *meta_dbt;
	loff_t dir_size;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "path: %s\n", dentry->d_name.name);
#endif
	dir_meta_dbt = lightfs_get_read_lock(LIGHTFS_I(dir));
	meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));

	dir_size = i_size_read(dir);

	if (!meta_key_is_root(meta_dbt->data)) {
		struct lightfs_sb_info *sbi = inode->i_sb->s_fs_info;
		DBT indirect_dbt;
		DB_TXN *txn;
		pr_info("WATH\n");

		ret = alloc_child_meta_dbt_from_inode(&indirect_dbt, dir, dentry->d_name.name);
		if (ret)
			goto out;
		TXN_GOTO_LABEL(retry);
		lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
		ret = lightfs_bstore_meta_del(sbi->meta_db, &indirect_dbt, txn, 0, S_ISDIR(inode->i_mode));
		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
		} else {
			ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
			COMMIT_JUMP_ON_CONFLICT(ret, retry);
		}
		dbt_destroy(&indirect_dbt);
out:
		lightfs_put_read_lock(LIGHTFS_I(inode));
		lightfs_put_read_lock(LIGHTFS_I(dir));
	} else {
		lightfs_put_read_lock(LIGHTFS_I(inode));
		lightfs_put_read_lock(LIGHTFS_I(dir));
	}


	if (ret) {
		pr_info("??????\n");
		return ret;
	}
	drop_nlink(inode);
	mark_inode_dirty(inode);
	
	i_size_write(dir, dir_size - 1);
	mark_inode_dirty(dir);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

static struct dentry *
lightfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	int r, err;
	struct dentry *ret;
	struct inode *inode;
	struct lightfs_sb_info *sbi = dir->i_sb->s_fs_info;
	DBT *dir_meta_dbt, meta_dbt;
	DB_TXN *txn;
	struct lightfs_metadata meta;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "path: %s\n", dentry->d_name.name);
#endif
	dir_meta_dbt = lightfs_get_read_lock(LIGHTFS_I(dir));
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	r = alloc_child_meta_dbt_from_inode(&meta_dbt, dir, dentry->d_name.name);
	if (r) {
		inode = ERR_PTR(r);
		goto out;
	}

	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_READONLY);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	r = lightfs_bstore_meta_get(sbi->meta_db, &meta_dbt, txn, &meta);
	if (r == -ENOENT) {
		inode = NULL;
		dbt_destroy(&meta_dbt);
		goto commit;
	} else if (r) {
abort:
		inode = ERR_PTR(r);
		lightfs_bstore_txn_abort(txn);
		dbt_destroy(&meta_dbt);
		goto out;
	} else if (meta.type == LIGHTFS_METADATA_TYPE_REDIRECT) {
		copy_meta_dbt_from_ino(&meta_dbt, meta.u.ino);
		r = lightfs_bstore_meta_get(sbi->meta_db, &meta_dbt, txn, &meta);
		BUG_ON(r == -ENOENT);
		if (r)
			goto abort;
	}

	BUG_ON(meta.type != LIGHTFS_METADATA_TYPE_NORMAL);
commit:
	err = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
	COMMIT_JUMP_ON_CONFLICT(err, retry);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	// r == -ENOENT, inode == 0
	// r == 0, get meta, need to setup inode
	// r == err, error, will not execute this code
	if (r == 0) {
		inode = lightfs_setup_inode(dir->i_sb, &meta_dbt, &meta);
		if (IS_ERR(inode))
			dbt_destroy(&meta_dbt);
	}
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

out:
	lightfs_put_read_lock(LIGHTFS_I(dir));
	if (IS_ERR(inode)) {
		pr_info("inode error: %p\n", inode);
	}
	ret = d_splice_alias(inode, dentry);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif

	return ret;
}

static int lightfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int ret;
	struct inode *inode = dentry->d_inode;
	loff_t size;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	ret = setattr_prepare(dentry, iattr);

	if (ret)
		return ret;

	if (is_quota_modification(inode, iattr)) {
		ret = dquot_initialize(inode);
		if (ret)
			return ret;
	}

	size = i_size_read(inode);
	if ((iattr->ia_valid & ATTR_SIZE) && iattr->ia_size < size) {
		uint64_t block_num;
		size_t block_off;
		loff_t size;
		struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
		struct lightfs_sb_info *sbi = inode->i_sb->s_fs_info;
		DBT *meta_dbt;
		DB_TXN *txn = NULL;

		size = i_size_read(inode);
		if (iattr->ia_size >= size) {
			goto skip_txn;
		}
		block_num = block_get_num_by_position(iattr->ia_size);
		block_off = block_get_off_by_position(iattr->ia_size);


		meta_dbt = lightfs_get_read_lock(lightfs_inode);
		ret = lightfs_bstore_trunc(sbi->data_db, meta_dbt, txn,
		                        block_num, block_off, inode);
		lightfs_put_read_lock(lightfs_inode);

skip_txn:
		i_size_write(inode, iattr->ia_size);
	}

	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return ret;
}

static int lightfs_getattr(const struct path *path, struct kstat *stat,
		        u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	unsigned int flags;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif


#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	flags = inode->i_flags & (FS_FL_USER_VISIBLE | FS_PROJINHERIT_FL);
	if (flags & FS_APPEND_FL)
		stat->attributes |= STATX_ATTR_APPEND;
	if (flags & FS_COMPR_FL)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (flags & FS_IMMUTABLE_FL)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (flags & FS_NODUMP_FL)
		stat->attributes |= STATX_ATTR_NODUMP;

	stat->attributes_mask |= (STATX_ATTR_APPEND |
			      STATX_ATTR_COMPRESSED |
			      STATX_ATTR_ENCRYPTED |
			      STATX_ATTR_IMMUTABLE |
			      STATX_ATTR_NODUMP);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	generic_fillattr(inode, stat);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif

	return 0;
}

static void lightfs_put_link(void *arg) {

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	kfree(arg);
}

static const char *lightfs_get_link(struct dentry *dentry, 
		         struct inode *inode, 
				 struct delayed_call *done)
{
	int r;
	char *ret;
	void *buf;
	struct lightfs_sb_info *sbi;
	struct lightfs_inode *lightfs_inode;
	DBT *meta_dbt;
	DBT data_dbt;
	DB_TXN *txn;

#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	if (!dentry) {
		return ERR_PTR(-ECHILD);
	}

	sbi = dentry->d_sb->s_fs_info;
	lightfs_inode = LIGHTFS_I(dentry->d_inode);

	buf = kmalloc(LIGHTFS_BSTORE_BLOCKSIZE, GFP_NOIO);
	if (!buf) {
		ret = ERR_PTR(-ENOMEM);
		goto err1;
	}
	meta_dbt = lightfs_get_read_lock(lightfs_inode);
	// now block start from 1
	r = alloc_data_dbt_from_inode(&data_dbt, dentry->d_inode, 1);
	if (r) {
		ret = ERR_PTR(r);
		goto err2;
	}
	//print_key(__func__, meta_dbt->data, meta_dbt->size);	
	//print_key(__func__, data_dbt.data, data_dbt.size);	
	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_READONLY);
	r = lightfs_bstore_get(sbi->data_db, &data_dbt, txn, buf, dentry->d_inode);
	if (r) {
		DBOP_JUMP_ON_CONFLICT(r, retry);
		lightfs_bstore_txn_abort(txn);
		ret = ERR_PTR(r);
		goto err3;
	}
	r = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
	COMMIT_JUMP_ON_CONFLICT(r, retry);

	set_delayed_call(done, lightfs_put_link, buf);
	ret = buf;

err3:
	dbt_destroy(&data_dbt);
err2:
	lightfs_put_read_lock(lightfs_inode);
	if (ret != buf) {
		do_delayed_call(done);
		clear_delayed_call(done);
	}

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
err1:
	return ret;
}

static struct inode *lightfs_alloc_inode(struct super_block *sb)
{
	struct lightfs_inode *lightfs_inode;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	lightfs_inode = kmem_cache_alloc(lightfs_inode_cachep, GFP_NOIO);
	if (!lightfs_inode) {
		pr_info("inode failed\n");
	}

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return lightfs_inode ? &lightfs_inode->vfs_inode : NULL;
}

static void lightfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(lightfs_inode_cachep, LIGHTFS_I(inode));
}

static void lightfs_destroy_inode(struct inode *inode)
{
	struct lightfs_inode *lightfs_inode = LIGHTFS_I(inode);
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	lightfs_get_write_lock(lightfs_inode);
	if (lightfs_inode->meta_dbt.data &&
	    lightfs_inode->meta_dbt.data != &root_meta_key)
		dbt_destroy(&lightfs_inode->meta_dbt);
	lightfs_put_write_lock(lightfs_inode);

	call_rcu(&inode->i_rcu, lightfs_i_callback);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
}

static int
lightfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int ret = 0;
	DB_TXN *txn;
	DBT *meta_dbt;
	struct lightfs_metadata meta;
	struct lightfs_sb_info *sbi = inode->i_sb->s_fs_info;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	if (inode->i_nlink == 0)
		goto no_write;


	meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));


	lightfs_copy_metadata_from_inode(&meta, inode);

#ifdef DISABLE_DCACHE
	lightfs_bstore_meta_put_cache(sbi->meta_db, meta_dbt, txn, &meta, NULL, false);
	lightfs_put_read_lock(LIGHTFS_I(inode));
	return 0;
#endif
	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
	ret = lightfs_bstore_meta_put(sbi->meta_db, meta_dbt, txn, &meta, NULL, false); // TODO: have to know parents inode
	if (ret) {
		DBOP_JUMP_ON_CONFLICT(ret, retry);
		lightfs_bstore_txn_abort(txn);
	} else {
		ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
		COMMIT_JUMP_ON_CONFLICT(ret, retry);
	}

	lightfs_put_read_lock(LIGHTFS_I(inode));

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
no_write:
	return ret;
}

static void lightfs_evict_inode(struct inode *inode)
{
	int ret;
	struct lightfs_sb_info *sbi = inode->i_sb->s_fs_info;
	DBT *meta_dbt;
	DB_TXN *txn;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif


#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif

	if (inode->i_nlink || (LIGHTFS_I(inode)->lightfs_flags & LIGHTFS_FLAG_DELETED)) {
		lightfs_bstore_meta_del(sbi->cache_db, &(LIGHTFS_I(inode)->meta_dbt), NULL, 1, S_ISDIR(inode->i_mode));
#ifdef GROUP_EVICTION
		if (S_ISDIR(inode->i_mode)) {
			lightfs_bstore_group_eviction(inode);
		}
#endif
		goto no_delete;
	}


	meta_dbt = lightfs_get_read_lock(LIGHTFS_I(inode));

	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
	ret = lightfs_do_unlink(meta_dbt, txn, inode, sbi);
	if (ret) {
		DBOP_JUMP_ON_CONFLICT(ret, retry);
		lightfs_bstore_txn_abort(txn);
	} else {
		ret = lightfs_bstore_txn_commit(txn, DB_TXN_NOSYNC);
		COMMIT_JUMP_ON_CONFLICT(ret, retry);
	}

	lightfs_put_read_lock(LIGHTFS_I(inode));

no_delete:
	truncate_inode_pages(&inode->i_data, 0);

	invalidate_inode_buffers(inode);
	clear_inode(inode);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
}

// called when VFS wishes to free sb (unmount), sync southbound here
static void lightfs_put_super(struct super_block *sb)
{
	struct lightfs_sb_info *sbi = sb->s_fs_info;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	sync_filesystem(sb);

	sb->s_fs_info = NULL;

	lightfs_bstore_env_close(sbi);

	free_percpu(sbi->s_lightfs_info);
	kfree(sbi);

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
}

static int lightfs_sync_fs(struct super_block *sb, int wait)
{
#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	return 0;
}

static int lightfs_super_statfs(struct dentry *d, struct kstatfs *buf) {
	return 0;
}

static int lightfs_dir_release(struct inode *inode, struct file *filp)
{
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

#ifdef CALL_TRACE
	lightfs_error(__func__, "\n");
#endif
	if (filp->f_pos != 0 && filp->f_pos != 1) {
		kfree((char *)filp->f_pos);
	}

#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif
	return 0;
}

static const struct address_space_operations lightfs_aops = {
	.readpage		= lightfs_readpage,
	.readpages		= lightfs_readpages,
	.writepage		= lightfs_writepage,
	.writepages		= lightfs_writepages,
	.write_begin		= lightfs_write_begin,
	.write_end		= lightfs_write_end,
	.launder_page		= lightfs_launder_page,
	.set_page_dirty = __set_page_dirty_nobuffers,
};

static const struct file_operations lightfs_file_file_operations = {
	.llseek			= generic_file_llseek,
	.fsync			= lightfs_fsync,
	.read_iter		= generic_file_read_iter,
	.write_iter		= generic_file_write_iter,
	.mmap			= generic_file_mmap,
};

static const struct file_operations lightfs_dir_file_operations = {
	.read			= generic_read_dir,
	.iterate		= lightfs_readdir,
	.fsync			= lightfs_fsync,
	.release		= lightfs_dir_release,
};

static const struct inode_operations lightfs_file_inode_operations = {
	.setattr		= lightfs_setattr
};

static const struct inode_operations lightfs_dir_inode_operations = {
	.create			= lightfs_create,
	.lookup			= lightfs_lookup,
	.unlink			= lightfs_unlink,
	.symlink		= lightfs_symlink,
	.mkdir			= lightfs_mkdir,
	.rmdir			= lightfs_rmdir,
	.mknod			= lightfs_mknod,
	.rename			= lightfs_rename,
	.setattr		= lightfs_setattr,
	.getattr		= lightfs_getattr,
};

static const struct inode_operations lightfs_symlink_inode_operations = {
	.get_link		= lightfs_get_link,
	.setattr		= lightfs_setattr,
	.getattr		= lightfs_getattr,
};

static const struct inode_operations lightfs_special_inode_operations = {
	.setattr		= lightfs_setattr,
};

static const struct super_operations lightfs_super_ops = {
	.alloc_inode		= lightfs_alloc_inode,
	.destroy_inode		= lightfs_destroy_inode,
	.write_inode		= lightfs_write_inode,
	.evict_inode		= lightfs_evict_inode,
	.put_super		= lightfs_put_super,
	.sync_fs		= lightfs_sync_fs,
	.statfs			= lightfs_super_statfs,
};

/*
 * fill inode with meta_key, metadata from database and inode number
 */
static struct inode *
lightfs_setup_inode(struct super_block *sb, DBT *meta_dbt,
                 struct lightfs_metadata *meta)
{
	struct inode *i;
	struct lightfs_inode *lightfs_inode;
#ifdef CALL_TRACE_TIME
	struct time_break tb; 
	lightfs_tb_init(&tb);
	lightfs_tb_check(&tb);
#endif

	if ((i = iget_locked(sb, meta->u.st.st_ino)) == NULL)
		return ERR_PTR(-ENOMEM);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	lightfs_inode = LIGHTFS_I(i);
	if (!(i->i_state & I_NEW)) {
		DBT *old_dbt;
		lightfs_put_read_lock(lightfs_inode);
	       	old_dbt = lightfs_get_write_lock(lightfs_inode);
		//print_key(__func__, old_dbt->data, old_dbt->size);
		//print_key(__func__, meta_dbt->data, meta_dbt->size);
		dbt_destroy(old_dbt);
		dbt_copy(old_dbt, meta_dbt);
		lightfs_put_write_lock(lightfs_inode);
		return i;
	}
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	//BUG_ON(lightfs_inode->meta_dbt.data != NULL);
	dbt_copy(&lightfs_inode->meta_dbt, meta_dbt);
	init_rwsem(&lightfs_inode->key_lock);
#ifdef READA
	lightfs_inode->ra_entry = NULL;
	//lightfs_inode->reada_state = READA_EMPTY;
	spin_lock_init(&lightfs_inode->reada_spin);
	INIT_LIST_HEAD(&lightfs_inode->ra_list);
	lightfs_inode->ra_entry_cnt = 0;
	lightfs_inode->last_block_start = lightfs_inode->last_block_len = 0;
	lightfs_inode->is_lookuped = 0;
#endif
	INIT_LIST_HEAD(&lightfs_inode->rename_locked);
	lightfs_inode->lightfs_flags = 0;
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	BUG_ON(meta->type != LIGHTFS_METADATA_TYPE_NORMAL);
	i->i_rdev = meta->u.st.st_dev;
	i->i_mode = meta->u.st.st_mode;
	set_nlink(i, meta->u.st.st_nlink);
#ifdef CONFIG_UIDGID_STRICT_TYPE_CHECKS
	i->i_uid.val = meta->u.st.st_uid;
	i->i_gid.val = meta->u.st.st_gid;
#else
	i->i_uid = make_kuid(i->i_sb->s_user_ns, meta->u.st.st_uid);
	i->i_gid = make_kgid(i->i_sb->s_user_ns, meta->u.st.st_gid);
#endif
	i->i_size = meta->u.st.st_size;
	i->i_blocks = meta->u.st.st_blocks;
	TIME_T_TO_TIMESPEC(i->i_atime, meta->u.st.st_atime);
	TIME_T_TO_TIMESPEC(i->i_mtime, meta->u.st.st_mtime);
	TIME_T_TO_TIMESPEC(i->i_ctime, meta->u.st.st_ctime);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	if (S_ISREG(i->i_mode)) {
		/* Regular file */
		i->i_op = &lightfs_file_inode_operations;
		i->i_fop = &lightfs_file_file_operations;
		i->i_data.a_ops = &lightfs_aops;
	} else if (S_ISDIR(i->i_mode)) {
		/* Directory */
		i->i_op = &lightfs_dir_inode_operations;
		i->i_fop = &lightfs_dir_file_operations;
	} else if (S_ISLNK(i->i_mode)) {
		/* Sym link */
		i->i_op = &lightfs_symlink_inode_operations;
		i->i_data.a_ops = &lightfs_aops;
	} else if (S_ISCHR(i->i_mode) || S_ISBLK(i->i_mode) ||
	           S_ISFIFO(i->i_mode) || S_ISSOCK(i->i_mode)) {
		i->i_op = &lightfs_special_inode_operations;
		init_special_inode(i, i->i_mode, i->i_rdev); // duplicates work
	} else {
		BUG();
	}
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
#endif

	unlock_new_inode(i);
#ifdef CALL_TRACE_TIME
	lightfs_tb_check(&tb);
	lightfs_tb_print(__func__, &tb);
#endif

	return i;
}


/*
 * fill in the superblock
 */
static int lightfs_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret;
	int cpu;
	ino_t ino = LIGHTFS_INO_CUR;
	struct inode *root;
	struct lightfs_metadata meta;
	struct lightfs_sb_info *sbi;
	DBT root_dbt;
	DB_TXN *txn;

	// LIGHTFS specific info
	ret = -ENOMEM;
	sbi = kzalloc(sizeof(struct lightfs_sb_info), GFP_NOIO);
	if (!sbi)
		goto err;

	sbi->s_lightfs_info = alloc_percpu(struct lightfs_info);
	if (!sbi->s_lightfs_info)
		goto err;

	sb->s_fs_info = sbi;
	sb_set_blocksize(sb, LIGHTFS_BSTORE_BLOCKSIZE);
	sb->s_op = &lightfs_super_ops;
	sb->s_maxbytes = MAX_LFS_FILESIZE;

	ret = lightfs_bstore_env_open(sbi);
	if (ret) {
		goto err;
	}

	TXN_GOTO_LABEL(retry);
	lightfs_bstore_txn_begin(sbi->db_env, NULL, &txn, TXN_MAY_WRITE);
	dbt_setup(&root_dbt, &root_meta_key, SIZEOF_ROOT_META_KEY);
	ret = lightfs_bstore_meta_get(sbi->meta_db, &root_dbt, txn, &meta);
	if (ret) {
		if (ret == -ENOENT) {
			lightfs_setup_metadata(&meta, 0755 | S_IFDIR, 0, 0,
			                    LIGHTFS_ROOT_INO);
			ret = lightfs_bstore_meta_put(sbi->meta_db,
			                           &root_dbt,
			                           txn, &meta, NULL, true);
		}
		if (ret) {
			DBOP_JUMP_ON_CONFLICT(ret, retry);
			lightfs_bstore_txn_abort(txn);
			goto err;
		}
	}
	ret = lightfs_bstore_txn_commit(txn, DB_TXN_SYNC);
	COMMIT_JUMP_ON_CONFLICT(ret, retry);

	sbi->s_nr_cpus = 0;
	for_each_possible_cpu(cpu) {
		(per_cpu_ptr(sbi->s_lightfs_info, cpu))->next_ino = ino + cpu;
		(per_cpu_ptr(sbi->s_lightfs_info, cpu))->max_ino = LIGHTFS_INO_MAX;
		sbi->s_nr_cpus++;
	}

	root = lightfs_setup_inode(sb, &root_dbt, &meta);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto err_close;
	}

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		ret = -EINVAL;
		goto err_close;
	}

	return 0;

err_close:
	lightfs_bstore_env_close(sbi);
err:
	if (sbi) {
		if (sbi->s_lightfs_info)
			free_percpu(sbi->s_lightfs_info);
		kfree(sbi);
	}
	return ret;
}

/*
 * mount lightfs, call kernel util mount_bdev
 * actual work of lightfs is done in lightfs_fill_super
 */
static struct dentry *lightfs_mount(struct file_system_type *fs_type, int flags,
                                 const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, lightfs_fill_super);
}

static void lightfs_kill_sb(struct super_block *sb)
{
	sync_filesystem(sb);
	kill_block_super(sb);
}

static struct file_system_type lightfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "lightfs",
	.mount		= lightfs_mount,
	.kill_sb	= lightfs_kill_sb,
	.fs_flags	= FS_REQUIRES_DEV,
};

int init_lightfs_fs(void)
{
	int ret;

	lightfs_inode_cachep =
		kmem_cache_create("lightfs_i",
		                  sizeof(struct lightfs_inode), 0,
		                  SLAB_RECLAIM_ACCOUNT,
		                  lightfs_i_init_once);
	if (!lightfs_inode_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize inode cache.\n");
		ret = -ENOMEM;
		goto out;
	}

	lightfs_writepages_cachep =
		kmem_cache_create("lightfs_wp",
		                  sizeof(struct lightfs_wp_node), 0,
		                  SLAB_RECLAIM_ACCOUNT,
		                  NULL);
	if (!lightfs_writepages_cachep) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to initialize write page vec cache.\n");
		ret = -ENOMEM;
		goto out_free_inode_cachep;
	}

	ret = register_filesystem(&lightfs_fs_type);
	if (ret) {
		printk(KERN_ERR "LIGHTFS ERROR: Failed to register filesystem\n");
		goto out_free_writepages_cachep;
	}

	return 0;

out_free_writepages_cachep:
	kmem_cache_destroy(lightfs_writepages_cachep);
out_free_inode_cachep:
	kmem_cache_destroy(lightfs_inode_cachep);
out:
	return ret;
}

void exit_lightfs_fs(void)
{
	unregister_filesystem(&lightfs_fs_type);

	kmem_cache_destroy(lightfs_writepages_cachep);

	kmem_cache_destroy(lightfs_inode_cachep);
}

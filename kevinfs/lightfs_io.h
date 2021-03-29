#ifndef __LIGHTFS_IO_H__
#define __LIGHTFS_IO_H__

#include "lightfs.h"
#include "./cheeze/cheeze.h"

int lightfs_io_create (DB_IO **db_io);

static inline void lightfs_io_print (char *buf, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		lightfs_error(__func__, "%d\n", buf[i]);
	}
}

static inline int lightfs_io_set_txn_id(char *buf, uint32_t txn_id, int idx)
{
	*((uint32_t *)buf) = txn_id;
	return idx + sizeof(uint32_t);
}

static inline int lightfs_io_set_cnt(char *buf, uint16_t cnt, int idx)
{
	*((uint16_t *)buf) = cnt;
	return idx + sizeof(uint16_t);
}

static inline int lightfs_io_set_type(char *buf, uint8_t type, int idx)
{
	*((uint8_t *)buf) = type;
	if (type > 16) {
		pr_info("TYPE:%d\n", type);
		BUG_ON(1);
	}
	return idx + sizeof(uint8_t);
}

static inline int lightfs_io_set_key_len(char *buf, uint16_t key_len, int idx)
{
	*((uint16_t *)buf) = key_len;
	return idx + sizeof(uint16_t);
}

static inline int lightfs_io_set_key(char *buf, uint16_t key_len, char *key, int idx)
{
	memcpy(buf, key, key_len);
	return idx + key_len;
}

static inline int lightfs_io_set_off(char *buf, uint16_t off, int idx)
{
	*((uint16_t *)buf) = off;
	return idx + sizeof(uint16_t);
}

static inline int lightfs_io_set_value_len(char *buf, uint16_t value_len, int idx)
{
	*((uint16_t *)buf) = value_len;
	return idx + sizeof(uint16_t);
}

static inline int lightfs_io_set_value(char *buf, uint16_t value_len, char *value, int idx)
{
	memcpy(buf, value, value_len); // TMP
	return idx + value_len;
}

static inline int lightfs_io_meta_set_value(char *buf, uint16_t value_len, char *value, int idx)
{
	memcpy(buf, value, value_len);
	return idx + 4096;
}

static inline int lightfs_io_set_buf_ptr(char *buf, char *buf_ptr, int idx)
{
	*((char **)buf) = buf_ptr;
	return idx + sizeof(char *);
}

// should set txn_id before calling this function
// SET: all
// UPDATE: all
// DEL: type, key_len, key
// DEL_MULTI: type, key_len, key, off (count of deleted key)
static inline int lightfs_io_set_buf_set(char *buf, uint8_t type, uint16_t key_len, char *key, uint16_t off, uint16_t value_len, char *value, int idx)
{
	idx = lightfs_io_set_type(buf + idx, type, idx);
	idx = lightfs_io_set_key_len(buf + idx, key_len, idx);
	idx = lightfs_io_set_key(buf + idx, key_len, key, idx);
	idx = lightfs_io_set_off(buf + idx, off, idx);
	idx = lightfs_io_set_value_len(buf + idx, value_len, idx);
	idx = lightfs_io_set_value(buf + idx, value_len, value, idx);

	return idx;
}

static inline int lightfs_io_set_buf_meta_set(char *buf, uint8_t type, uint16_t key_len, char *key, uint16_t off, uint16_t value_len, char *value, int idx)
{
	idx = lightfs_io_set_type(buf + idx, type, idx);
	idx = lightfs_io_set_key_len(buf + idx, key_len, idx);
	idx = lightfs_io_set_key(buf + idx, key_len, key, idx);
	idx = lightfs_io_set_off(buf + idx, off, idx);
	idx = lightfs_io_set_value_len(buf + idx, 152, idx);
	idx = lightfs_io_meta_set_value(buf + idx, 152, value, idx);

	return idx;
}


static inline int lightfs_io_set_buf_update(char *buf, uint8_t type, uint16_t key_len, char *key, uint16_t off, uint16_t value_len, char *value, int idx)
{
	idx = lightfs_io_set_type(buf + idx, type, idx);
	idx = lightfs_io_set_key_len(buf + idx, key_len, idx);
	idx = lightfs_io_set_key(buf + idx, key_len, key, idx);
	idx = lightfs_io_set_off(buf + idx, off, idx);
	idx = lightfs_io_set_value_len(buf + idx, value_len, idx);
	idx = lightfs_io_set_value(buf + idx, 4096, value, idx);
	return idx;
}


static inline int lightfs_io_set_buf_del(char *buf, uint8_t type, uint16_t key_len, char *key, int idx)
{
	idx = lightfs_io_set_type(buf + idx, type, idx);
	idx = lightfs_io_set_key_len(buf + idx, key_len, idx);
	idx = lightfs_io_set_key(buf + idx, key_len, key, idx);
	return idx;
}

static inline int lightfs_io_set_buf_del_multi(char *buf, uint8_t type, uint16_t key_len, char *key, uint16_t off, int idx)
{
	idx = lightfs_io_set_type(buf + idx, type, idx);
	idx = lightfs_io_set_key_len(buf + idx, key_len, idx);
	idx = lightfs_io_set_key(buf + idx, key_len, key, idx);
	idx = lightfs_io_set_off(buf + idx, off, idx);
	return idx;
}

static inline int lightfs_io_set_buf_get(char *buf, uint8_t type, uint16_t key_len, char *key, uint16_t value_len, int idx)
{
	idx = lightfs_io_set_type(buf + idx, type, idx);
	idx = lightfs_io_set_key_len(buf + idx, key_len, idx);
	idx = lightfs_io_set_key(buf + idx, key_len, key, idx);
	idx = lightfs_io_set_value_len(buf + idx, value_len, idx);
	return idx;
}

static inline int lightfs_io_set_buf_iter(char *buf, uint8_t type, uint16_t key_len, char *key, uint16_t off, uint16_t value_len, int idx)
{
	idx = lightfs_io_set_type(buf + idx, type, idx);
	idx = lightfs_io_set_key_len(buf + idx, key_len, idx);
	idx = lightfs_io_set_key(buf + idx, key_len, key, idx);
	idx = lightfs_io_set_off(buf + idx, off, idx);
	idx = lightfs_io_set_value_len(buf + idx, value_len, idx);
	return idx;
}

static inline int lightfs_io_set_buf_get_multi(char *buf, uint8_t type, uint16_t key_len, char *key, int idx)
{
	idx = lightfs_io_set_type(buf + idx, type, idx);
	idx = lightfs_io_set_key_len(buf + idx, key_len, idx);
	idx = lightfs_io_set_key(buf + idx, key_len, key, idx);
	return idx;
}

static inline void lightfs_io_set_cheeze_req(struct cheeze_req_user *req, int buf_len, char *buf, char *ret_buf, int ubuf_len)
{
	req->buf_len = buf_len;
	req->buf = buf;
	req->ret_buf = ret_buf;
	req->ubuf_len = ubuf_len;
}


#endif

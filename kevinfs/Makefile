ifdef M
	include $(M)/mkinclude
else
	include $(PWD)/mkinclude
endif

obj-m += lightfs.o


KBUILD_CFLAGS +=  -I./cheeze \
				  -DLIGHTFS \
				  -DPINK \
				  -DWB \
				  -DGROUP_COMMIT \
				  -DGET_MULTI \
				  -DRB_LOCK \
				  -DSUPER_NOLOCK \
				  -DREADA \
#				  -DMONITOR \
#				  -DIS_IN_VM \
#				  -DPRINT_QD \
#				  -DDISABLE_DCACHE \
#				  -DLIGHTFS_DEBUG \
#				  -DDATA_CHECK \
#				  -DCALL_TRACE \
#				  -DCALL_TRACE_TIME \
#				  -DTIME_CHECK \
#				  -DTXN_BUFFER \
#				  -DGROUP_EVICTION \
#				  -DTXN_TIME_CHECK \
#				  -DRB_CACHE \
#				  -DCHEEZE \
#				  -DEMULATION \

lightfs-y := lightfs_super.o \
		  lightfs_bstore.o \
		  lightfs_reada.o \
		  lightfs_txn_hdlr.o \
		  lightfs_io.o \
		  lightfs_db.o \
		  lightfs_db_env.o \
		  lightfs_cache.o \
		  bloomfilter.o \
		  lightfs_queue.o \
		  murmur3.o \
		  rbtreekv.o \
		  ./cheeze/queue.o \
		  ./cheeze/blk.o \
		  ./cheeze/shm.o \
		  lightfs_module.o \

all:
	make -C /lib/modules/$(KDIR)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(KDIR)/build M=$(PWD) clean

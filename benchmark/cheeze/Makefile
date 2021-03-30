ifneq ($(KERNELRELEASE),)
	obj-m	 := cheeze.o
	cheeze-y := blk.o queue.o shm.o

	# EXTRA_CFLAGS += -DDEBUG
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD :=$(shell pwd)
	TARGET_PATH := kernel/drivers/misc
	INBOXDRIVER := $(shell find $(subst build,$(TARGET_PATH),$(KERNELDIR)) -name cheeze.ko.* -type f)

.PHONY: modules
modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

.PHONY: all
all: clean modules install

.PHONY: clean
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

.PHONY: install
install:
ifneq ($(shell lsmod | grep cheeze),)
	rmmod cheeze
endif
ifneq ($(INBOXDRIVER),)
	rm -f $(INBOXDRIVER)
endif
	$(MAKE) -C $(KERNELDIR) M=$(PWD) INSTALL_MOD_DIR=$(TARGET_PATH) modules_install
	modprobe cheeze

.PHONY: install_rules
install_rules:
	install --group=root --owner=root --mode=0644 $(RULEFILE) $(RULEDIR)

endif


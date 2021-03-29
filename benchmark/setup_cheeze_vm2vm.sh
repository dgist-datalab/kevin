#!/bin/bash

set -eo pipefail

rmmod cheeze 2>/dev/null || true
insmod /home/koo/cheeze/cheeze.ko

echo 0x800000000 > /sys/module/cheeze/parameters/page_addr
echo 1 > /sys/module/cheeze/parameters/enabled
echo $((128 * 1024 * 1024 * 1024)) > /sys/block/cheeze0/disksize

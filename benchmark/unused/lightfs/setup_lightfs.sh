#!/bin/bash

set -eo pipefail

rmmod cheeze 2>/dev/null || true
insmod /home/koo/cheeze/cheeze.ko

TMP=/tmp/setup_vm

echo -n "/sys/module/cheeze/parameters/page_addr0: "
awk '{print $NF}' $TMP | head -n1 | tail -n1 | tee /sys/module/cheeze/parameters/page_addr0

echo -n "/sys/module/cheeze/parameters/page_addr1: "
awk '{print $NF}' $TMP | head -n2 | tail -n1 | tee /sys/module/cheeze/parameters/page_addr1

echo -n "/sys/module/cheeze/parameters/page_addr2: "
awk '{print $NF}' $TMP | head -n3 | tail -n1 | tee /sys/module/cheeze/parameters/page_addr2

echo 1 > /sys/module/cheeze/parameters/enabled
echo $((128 * 1024 * 1024 * 1024)) > /sys/block/cheeze0/disksize

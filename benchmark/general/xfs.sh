#!/bin/bash
#usage: ./ext4_no_journal <device>

sudo yes | mkfs.xfs -f $1
sudo mount -t xfs -o discard $1 /bench

#dumpe2fs $1 > dumpfs.out

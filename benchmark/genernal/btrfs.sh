#!/bin/bash
#usage: ./ext4_no_journal <device>

sudo yes | mkfs.btrfs -f $1
sudo mount -t btrfs -o discard $1 /bench

#dumpe2fs $1 > dumpfs.out

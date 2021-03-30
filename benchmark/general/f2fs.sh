#!/bin/bash
#usage: ./ext4_no_journal <device>

sudo yes | mkfs.f2fs -f $1
sudo mount -t f2fs -o discard $1 /bench

#dumpe2fs $1 > dumpfs.out

#!/bin/bash
#usage: ./ext4_no_journal <device>

sudo yes | mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 $1
#sudo yes | mkfs.ext4 -N 13000000 -E lazy_itable_init=0,lazy_journal_init=0 $1
sudo mount -t ext4 -o discard,data=journal $1 /bench

#dumpe2fs $1 > dumpfs.out
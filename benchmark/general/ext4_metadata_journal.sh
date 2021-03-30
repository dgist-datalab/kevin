#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi
#usage: ./ext4_no_journal <device>

sudo yes | mkfs.ext4 -E lazy_itable_init=0,lazy_journal_init=0 $1
sudo mount -t ext4 -o discard,data=ordered $1 /bench

#dumpe2fs $1 > dumpfs.out

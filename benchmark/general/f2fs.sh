#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi
#usage: ./ext4_no_journal <device>

sudo yes | mkfs.f2fs -f $1
sudo mount -t f2fs -o discard $1 /bench

#dumpe2fs $1 > dumpfs.out

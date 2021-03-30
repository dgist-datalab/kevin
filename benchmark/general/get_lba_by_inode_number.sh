#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi
#usage: ./get_lba_by_inode_number <device>

sudo debugfs -R "stat <$1>" /dev/cheeze0

#!/bin/bash
#usage: ./get_lba_by_inode_number <device>

sudo debugfs -R "stat <$1>" /dev/cheeze0

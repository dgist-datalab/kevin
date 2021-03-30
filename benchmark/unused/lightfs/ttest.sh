#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi
#mkdir /sys/fs/cgroup/bench
#echo "+memory" > /sys/fs/cgroup/cgroup.subtree_control
#echo $((16 * 1024 * 1024 * 1024)) > /sys/fs/cgroup/bench/memory.max
echo 0 > /proc/sys/kernel/randomize_va_space


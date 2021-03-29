#!/bin/bash
#mkdir /sys/fs/cgroup/bench
#echo "+memory" > /sys/fs/cgroup/cgroup.subtree_control
#echo $((16 * 1024 * 1024 * 1024)) > /sys/fs/cgroup/bench/memory.max
echo 0 > /proc/sys/kernel/randomize_va_space


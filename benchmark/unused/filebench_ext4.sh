#!/bin/bash

kevin_root_dir="/home/koo/src/koofs"
log_path="/log/filebench_log-$(date +'%Y%m%d-%H%M%S')"

dev_path="/dev/cheeze0"
ext4_dir="/bench"

remove_data() {
    rm -rf $ext4_dir/*
}

flush() {
    sync
    echo 3 > /proc/sys/vm/drop_caches
    echo 1 > /proc/sys/vm/compact_memory
    echo 3 > /proc/sys/vm/drop_caches
    echo 1 > /proc/sys/vm/compact_memory
}

destroy() {
    rm -rf /tmp/filebench-shm-*
    sleep 5
    umount $ext4_dir
    sleep 5
}

# Unused
setup_cgroups() {
    mkdir /sys/fs/cgroup/bench
    echo "+memory" > /sys/fs/cgroup/cgroup.subtree_control
    echo $((16 * 1024 * 1024 * 1024)) > /sys/fs/cgroup/bench/memory.max
}

do_ext4() {
    for workload in *.f
    do
        echo "=============================================="
        echo ${workload}
        mkdir -p "$output_dir_org_perf" "$output_dir_org_cnt" "$output_dir_org_kukania" "$output_dir_org_stat"
        output_file_perf="${output_dir_org_perf}/${workload}"
        output_file_cnt="${output_dir_org_cnt}/${workload}"
        output_file_kukania="${output_dir_org_kukania}/${workload}"
        output_file_stat="${output_dir_org_stat}/${workload}"
        cd /home/kukania/Koofs_proj/FlashFTLDriver/
        ./cheeze_block_driver > ${output_file_kukania} 2>&1 < /dev/null &
        cd -
        tail -f ${output_file_kukania} | sed '/now waiting req/ q'
        now=$(date +"%T")
        echo "Current time : $now" >> ${output_file_perf}
#    flush
        ${fs_sh} ${dev_path}
        #sleep 2000
        flush
        sleep 5
        $kevin_root_dir/benchmark/general/blktrace.sh ${dev_path} ${output_file_cnt}
        iostat -c -d -x ${dev_path} 1 -m > ${output_file_stat} &
        sleep 5
        #bash -c "echo "'$$'" > /sys/fs/cgroup/bench/cgroup.procs; filebench -f ${workload} >> ${output_file_perf}"
        bash -c "filebench -f ${workload} >> ${output_file_perf}"
        #sleep 300
        df >> ${output_file_perf}
        sleep 1
        ps -ef | grep blktrace | grep -v grep | awk '{print "kill -2 " $2}' | sh
        ps -ef | grep iostat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        destroy
        kill -2 $(pgrep -f cheeze_block_driver)
        while pgrep -f cheeze_block_driver > /dev/null; do sleep 1; done
        echo End workload
        sleep 5
    done
}

echo 0 > /proc/sys/kernel/randomize_va_space
setup_cgroups

# lock /bench in case of mount failures
umount -lf /bench 2>/dev/null
umount -lf /bench 2>/dev/null
umount -lf /bench 2>/dev/null
mount -t tmpfs -o ro nodev /bench

for test in ext4_data_journal ext4_metadata_journal xfs f2fs btrfs
do
    output_dir_org_perf="$log_path/$test/perf"
    output_dir_org_cnt="$log_path/$test/trace"
    output_dir_org_kukania="$log_path/$test/kukania"
    output_dir_org_stat="$log_path/$test/iostat"
    fs_sh="${kevin_root_dir}/benchmark/general/$test.sh"
    do_ext4
done

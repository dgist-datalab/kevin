#!/bin/bash

log_path="/log/filebench_vmlog-$(date +'%Y%m%d-%H%M%S')"
dev_path="/dev/cheeze0"
target_dir="/bench"

remove_data() {
    rm -rf $target_dir/*
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
    umount $target_dir
    sleep 1
    rmmod cheeze
    sleep 5
}

do_ext4() {
    for workload in *.f
    do
        echo "=============================================="
        echo ${workload}
        mkdir -p \
            "$output_dir_org_perf" \
            "$output_dir_org_cnt" \
            "$output_dir_org_flashdriver" \
            "$output_dir_org_stat" \
            "$output_dir_org_slab"
        output_file_perf="${output_dir_org_perf}/${workload}"
        output_file_cnt="${output_dir_org_cnt}/${workload}"
        output_file_flashdriver="${output_dir_org_flashdriver}/${workload}"
        output_file_stat="${output_dir_org_stat}/${workload}"
        output_file_slab="${output_dir_org_slab}/${workload}"

        ${kevin_root_dir}/benchmark/setup_cheeze.sh
        ssh root@pt1 "cd ${flash_ftl_driver_dir}/; ./cheeze_block_driver > /c0/$output_file_flashdriver 2>&1 < /dev/null" &
        while [ ! -f ${output_file_flashdriver} ]; do sleep 0.1; done
        tail -f ${output_file_flashdriver} | sed '/now waiting req/ q'
        now=$(date +"%T")
        echo "Current time : $now" >> ${output_file_perf}
        ${fs_sh} ${dev_path}
        #sleep 2000
        flush
        sleep 5
        $kevin_root_dir/benchmark/general/blktrace.sh ${dev_path} ${output_file_cnt}
        iostat -c -d -x ${dev_path} 1 -m > ${output_file_stat} &
        sleep 5

        filebench -f ${workload} >> ${output_file_perf}

        #sleep 300
        df >> ${output_file_perf}
        sleep 1
        ps -ef | grep blktrace | grep -v grep | awk '{print "kill -2 " $2}' | sh
        ps -ef | grep iostat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        while pgrep -f blktrace > /dev/null; do sleep 1; done
        slabtop -o --sort=c > ${output_file_slab} 2>&1

        destroy
        ssh root@pt1 'kill -2 $(pgrep -fx ./cheeze_block_driver); while pgrep -fx ./cheeze_block_driver > /dev/null; do sleep 1; done'
        echo End workload
        sleep 5
    done
}

echo 0 > /proc/sys/kernel/randomize_va_space

# lock /bench in case of mount failures
umount -lf /bench 2>/dev/null
umount -lf /bench 2>/dev/null
umount -lf /bench 2>/dev/null
mount -t tmpfs -o ro nodev /bench

for test in ext4_data_journal xfs f2fs btrfs ext4_metadata_journal
do
    output_dir_org_perf="$log_path/$test/perf"
    output_dir_org_cnt="$log_path/$test/trace"
    output_dir_org_flashdriver="$log_path/$test/flashdriver"
    output_dir_org_stat="$log_path/$test/iostat"
    output_dir_org_slab="$log_path/$test/slab"
    fs_sh="${kevin_root_dir}/benchmark/general/$test.sh"
    do_ext4
done

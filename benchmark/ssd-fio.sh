#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

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
    sleep 5
}

mkmount() {
    ${fs_sh} ${dev_path}
}

start_driver() {
    mkmount
}

stop_driver() {
    umount $target_dir
}

setup_log() {
    echo "=============================================="
    echo ${workload}
    mkdir -p \
        "$output_dir_org_perf" \
        "$output_dir_org_cnt" \
        "$output_dir_org_flashdriver" \
        "$output_dir_org_stat" \
        "$output_dir_org_vmstat" \
        "$output_dir_org_slab"
    output_file_perf="${output_dir_org_perf}/${alias}"
    output_file_cnt="${output_dir_org_cnt}/${alias}"
    output_file_flashdriver="${output_dir_org_flashdriver}/${alias}"
    output_file_stat="${output_dir_org_stat}/${alias}"
    output_file_vmstat="${output_dir_org_vmstat}/${alias}"
    output_file_slab="${output_dir_org_slab}/${alias}"
}

run_bench() {
    #for workload in *.f
    #do
        now=$(date +"%T")
        echo "Current time : $now" >> ${output_file_perf}

        flush
        sleep 300

        $kevin_root_dir/benchmark/general/blktrace.sh ${dev_path} ${output_file_cnt}
        iostat -c -d -x ${dev_path} 1 -m >> ${output_file_stat} &
        vmstat 1 | gawk '{now=strftime("%Y-%m-%d %T "); print now $0}' >> ${output_file_vmstat} &

        sleep 5

        fio ${workload} >> ${output_file_perf} 2>&1

        slabtop -o --sort=c >> ${output_file_slab} 2>&1

        sync

        ps -ef | grep blktrace | grep -v grep | awk '{print "kill -2 " $2}' | sh
        ps -ef | grep iostat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        ps -ef | grep vmstat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        while pgrep -f blktrace > /dev/null; do sleep 1; done

        echo End workload
        sleep 5
    #done
}

echo 0 > /proc/sys/kernel/randomize_va_space

# lock ${target_dir} in case of mount failures
umount -lf ${target_dir} 2>/dev/null
umount -lf ${target_dir} 2>/dev/null
umount -lf ${target_dir} 2>/dev/null
mount -t tmpfs -o ro nodev ${target_dir}

for dev in nvme-SAMSUNG_MZQLB3T8HALS-000KV_S4DRNY0KA00072-part1
do
    log_path="/log/filebench_${dev}-fiolog-$(date +'%Y%m%d-%H%M%S')"
    dev_path="/dev/disk/by-id/${dev}"

    echo "Running $dev"

    for test in ext4_data_journal ext4_metadata_journal xfs f2fs btrfs
    do
        output_dir_org_perf="$log_path/$test/perf"
        output_dir_org_cnt="$log_path/$test/trace"
        output_dir_org_flashdriver="$log_path/$test/flashdriver"
        output_dir_org_stat="$log_path/$test/iostat"
        output_dir_org_vmstat="$log_path/$test/vmstat"
        output_dir_org_slab="$log_path/$test/slab"
        fs_sh="${kevin_root_dir}/benchmark/general/$test.sh"

        alias=seqwrite
        setup_log
        start_driver

        workload=seqwrite.fio
        run_bench
        flush

        stop_driver
        alias=seqwrite_seqread
        setup_log
        start_driver

        workload=seqwrite.fio
        run_bench
        flush

        workload=seqread.fio
        run_bench
        flush

        stop_driver
        alias=seqwrite_randread
        setup_log
        start_driver

        workload=seqwrite.fio
        run_bench
        flush

        workload=randread.fio
        run_bench
        flush

        stop_driver
        alias=randwrite
        setup_log
        start_driver

        workload=randwrite.fio
        run_bench
        flush

        stop_driver
        alias=randwrite_seqread
        setup_log
        start_driver

        workload=randwrite.fio
        run_bench
        flush

        workload=seqread.fio
        run_bench
        flush

        stop_driver
        alias=randwrite_randread
        setup_log
        start_driver

        workload=randwrite.fio
        run_bench
        flush

        workload=randread.fio
        run_bench
        flush

        stop_driver
    done
done

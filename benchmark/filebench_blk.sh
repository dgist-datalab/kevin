#!/bin/bash

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
            "$output_dir_org_vmstat" \
            "$output_dir_org_slab"
        output_file_perf="${output_dir_org_perf}/${workload}"
        output_file_cnt="${output_dir_org_cnt}/${workload}"
        output_file_flashdriver="${output_dir_org_flashdriver}/${workload}"
        output_file_stat="${output_dir_org_stat}/${workload}"
        output_file_vmstat="${output_dir_org_vmstat}/${workload}"
        output_file_slab="${output_dir_org_slab}/${workload}"

        now=$(date +"%T")
        echo "Current time : $now" >> ${output_file_perf}
        ${fs_sh} ${dev_path}
        flush

        sleep 180

        $kevin_root_dir/benchmark/blktrace.sh ${dev_path} ${output_file_cnt}
        iostat -c -d -x ${dev_path} 1 -m > ${output_file_stat} &
        vmstat 1 | gawk '{now=strftime("%Y-%m-%d %T "); print now $0}' > ${output_file_vmstat} &

        sleep 5

        filebench -f ${workload} >> ${output_file_perf}

        #sleep 300
        df >> ${output_file_perf}

        #echo -n "Total directory count: " >> ${output_file_perf}
        #find ${target_dir} -type d | wc -l >> ${output_file_perf}

        #echo -n "Total file count: " >> ${output_file_perf}
        #find ${target_dir} -type f | wc -l >> ${output_file_perf}

        sleep 1

        slabtop -o --sort=c > ${output_file_slab} 2>&1

        destroy

        ps -ef | grep blktrace | grep -v grep | awk '{print "kill -2 " $2}' | sh
        ps -ef | grep iostat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        ps -ef | grep vmstat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        while pgrep -f blktrace > /dev/null; do sleep 1; done

        echo End workload
        sleep 5
    done
}

echo 0 > /proc/sys/kernel/randomize_va_space

# lock ${target_dir} in case of mount failures
umount -lf ${target_dir} 2>/dev/null
umount -lf ${target_dir} 2>/dev/null
umount -lf ${target_dir} 2>/dev/null
mount -t tmpfs -o ro nodev ${target_dir}

for dev in nvme-SAMSUNG_MZQLB3T8HALS-000KV_S4DRNY0KA00072-part1
do
    log_path="/log/filebench_${dev}-log-$(date +'%Y%m%d-%H%M%S')"
    dev_path="/dev/disk/by-id/${dev}"

    echo "Running $dev"

    #for test in ext4_data_journal ext4_metadata_journal xfs f2fs btrfs
    for test in btrfs xfs
    do
        output_dir_org_perf="$log_path/$test/perf"
        output_dir_org_cnt="$log_path/$test/trace"
        output_dir_org_flashdriver="$log_path/$test/flashdriver"
        output_dir_org_stat="$log_path/$test/iostat"
        output_dir_org_vmstat="$log_path/$test/vmstat"
        output_dir_org_slab="$log_path/$test/slab"
        fs_sh="${kevin_root_dir}/benchmark/$test.sh"
        do_ext4
    done
done

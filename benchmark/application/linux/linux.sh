#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

if [ -z "$1" ]; then
  echo "Please specify workload name for logging"
  exit 1
fi

log_path="/log/linux-$1-$(date +'%Y%m%d-%H%M%S')"
dev_path="/dev/cheeze0"
target_dir="/bench"

mkdir -p $log_path

echo "Saving output to $log_path/totallog"

case "$2" in
    -f)
        ;;
    *)
        echo "Daemonizing script"
        $0 $1 -f < /dev/null &> /dev/null & disown
        exit 0
        ;;
esac

exec > $log_path/totallog 2>&1

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
    ${kevin_root_dir}/benchmark/setup_cheeze.sh
    ssh root@pt1 "cd ${flash_ftl_driver_dir}/; ./cheeze_block_driver > /$(cat /etc/hostname)/$output_file_flashdriver 2>&1 < /dev/null" &
    while [ ! -f ${output_file_flashdriver} ]; do sleep 0.1; done
    tail -f ${output_file_flashdriver} | sed '/now waiting req/ q'
    mkmount
}

stop_driver() {
    umount $target_dir
    rmmod cheeze
    ssh root@pt1 'kill -2 $(pgrep -fx ./cheeze_block_driver); while pgrep -fx ./cheeze_block_driver > /dev/null; do sleep 1; done'
}

# FlashDriver logs are saved at copy workload
setup_log() {
    echo "=============================================="
    echo ${workload}
    mkdir -p \
        "$output_dir_org_perf" \
        "$output_dir_org_cnt" \
        "$output_dir_org_stat" \
        "$output_dir_org_vmstat" \
        "$output_dir_org_slab" \
        "$output_dir_org_dmesg"
    output_file_perf="${output_dir_org_perf}/${workload}"
    output_file_cnt="${output_dir_org_cnt}/${workload}"
    output_file_stat="${output_dir_org_stat}/${workload}"
    output_file_vmstat="${output_dir_org_vmstat}/${workload}"
    output_file_slab="${output_dir_org_slab}/${workload}"
    output_file_dmesg="${output_dir_org_dmesg}/${workload}"
    sync
}

clone() {
    \time -v bash -c "cd ${target_dir}; git clone ${kernel_src}; cd -"
}

build() {
    \time -v bash -c "cd ${target_dir}/linux-stable; make allyesconfig; make -j8; sync; cd -"
}

rsyncf() {
    \time -v rsync -a --fsync /linux/linux-stable/ ${target_dir}/
}

run_bench() {
    #for workload in *.f
    #do
        dmesg -c > /dev/null 2>&1

        flush
        sleep 5

        $kevin_root_dir/benchmark/general/blktrace.sh ${dev_path} ${output_file_cnt}
        iostat -c -d -x ${dev_path} 1 -m > ${output_file_stat} &
        vmstat 1 | gawk '{now=strftime("%Y-%m-%d %T "); print now $0}' > ${output_file_vmstat} &
        dmesg -w > ${output_file_dmesg} &

        sleep 5

        $workload > ${output_file_perf} 2>&1

        #sleep 300
        df >> ${output_file_perf}

        #echo -n "Total directory count: " >> ${output_file_perf}
        #find ${target_dir} -type d | wc -l >> ${output_file_perf}

        #echo -n "Total file count: " >> ${output_file_perf}
        #find ${target_dir} -type f | wc -l >> ${output_file_perf}

        sleep 1

        slabtop -o --sort=c > ${output_file_slab} 2>&1

        ps -ef | grep blktrace | grep -v grep | awk '{print "kill -2 " $2}' | sh
        ps -ef | grep iostat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        ps -ef | grep vmstat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        while pgrep -f blktrace > /dev/null; do sleep 1; done

        echo End workload

        ps -ef | grep dmesg | grep -v grep | awk '{print "kill -15 " $2}' | sh
        while pgrep -f dmesg > /dev/null; do sleep 1; done
        dmesg -c > /dev/null 2>&1

        sleep 5
    #done
}

echo 0 > /proc/sys/kernel/randomize_va_space

# lock ${target_dir} in case of mount failures
umount -lf ${target_dir} 2>/dev/null
umount -lf ${target_dir} 2>/dev/null
umount -lf ${target_dir} 2>/dev/null
mount -t tmpfs -o ro nodev ${target_dir}

for test in ext4_metadata_journal ext4_data_journal xfs f2fs btrfs
do
    output_dir_org_perf="$log_path/$test/perf"
    output_dir_org_cnt="$log_path/$test/trace"
    output_dir_org_stat="$log_path/$test/iostat"
    output_dir_org_vmstat="$log_path/$test/vmstat"
    output_dir_org_slab="$log_path/$test/slab"
    output_dir_org_dmesg="$log_path/$test/dmesg"
    output_file_flashdriver="$log_path/$test/flashdriver"

    fs_sh="${kevin_root_dir}/benchmark/general/$test.sh"

    setup_log

    start_driver

    workload=clone
    setup_log
    run_bench

    workload=build
    setup_log
    run_bench

    stop_driver
done

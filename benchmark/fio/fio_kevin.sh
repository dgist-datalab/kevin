#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

if [ -z "$1" ]; then
  echo "Please specify workload name for logging"
  exit 1
fi

log_path="/log/filebench_vmfio-kevin-log-$1-$(date +'%Y%m%d-%H%M%S')"
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

start_driver() {
    ssh root@pt1 "cd ${flash_driver_dir}/; ./koo_kv_driver -x -l 4 -c 3 -t 5 > /$(cat /etc/hostname)/$output_file_flashdriver 2>&1 < /dev/null" &
    while [ ! -f ${output_file_flashdriver} ]; do sleep 0.1; done
    tail -f ${output_file_flashdriver} | sed '/now waiting req/ q'

    cd ${kevin_root_dir}/kevinfs/
    ./run.sh ${target_dir}
    cd -
}

stop_driver() {
    rm -rf /tmp/filebench-shm-*
    sleep 60
    umount $target_dir
    sleep 10
    rmmod lightfs
    ssh root@pt1 'kill -2 $(pgrep -f '^./koo_kv_driver'); while pgrep -f '^./koo_kv_driver' > /dev/null; do sleep 1; done'
    sleep 5
}

setup_log() {
    echo "=============================================="
    echo ${workload}
    mkdir -p \
        "$output_dir_org_perf" \
        "$output_dir_org_flashdriver" \
        "$output_dir_org_vmstat" \
        "$output_dir_org_slab" \
        "$output_dir_org_dmesg"
    output_file_perf="${output_dir_org_perf}/${alias}"
    output_file_flashdriver="${output_dir_org_flashdriver}/${alias}"
    output_file_vmstat="${output_dir_org_vmstat}/${alias}"
    output_file_slab="${output_dir_org_slab}/${alias}"
    output_file_dmesg="${output_dir_org_dmesg}/${alias}"
}

run_bench() {
    #for workload in *.f
    #do
        dmesg -c > /dev/null 2>&1

        #sleep 2000
        flush

        vmstat 1 | gawk '{now=strftime("%Y-%m-%d %T "); print now $0}' >> ${output_file_vmstat} &
        dmesg -w > ${output_file_dmesg} &

        sleep 5

        fio ${workload} >> ${output_file_perf} 2>&1

        slabtop -o --sort=c >> ${output_file_slab} 2>&1

        sync

        ps -ef | grep vmstat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        ps -ef | grep dmesg | grep -v grep | awk '{print "kill -15 " $2}' | sh
        while pgrep -f dmesg > /dev/null; do sleep 1; done
        dmesg -c > /dev/null 2>&1

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

#for test in ext4_metadata_journal ext4_data_journal xfs f2fs btrfs
for test in ext4_data_journal xfs f2fs btrfs
do
    output_dir_org_perf="$log_path/$test/perf"
    output_dir_org_flashdriver="$log_path/$test/flashdriver"
    output_dir_org_vmstat="$log_path/$test/vmstat"
    output_dir_org_slab="$log_path/$test/slab"
    output_dir_org_dmesg="$log_path/$test/dmesg"

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

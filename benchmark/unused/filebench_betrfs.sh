#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

if [ -z "$1" ]; then
  echo "Please specify workload name for logging"
  exit 1
fi

kevin_root_dir="/home/koo/src/koofs"
log_path="/log/filebench_vmlog-$1-$(date +'%Y%m%d-%H%M%S')"
dev_path="/dev/cheeze0"
ext4_dir="/ext4"

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
    ${clean_sh}
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
            "$output_dir_org_kukania" \
            "$output_dir_org_stat" \
            "$output_dir_org_vmstat" \
            "$output_dir_org_slab" \
            "$output_dir_org_dmesg"
        output_file_perf="${output_dir_org_perf}/${workload}"
        output_file_cnt="${output_dir_org_cnt}/${workload}"
        output_file_kukania="${output_dir_org_kukania}/${workload}"
        output_file_stat="${output_dir_org_stat}/${workload}"
        output_file_vmstat="${output_dir_org_vmstat}/${workload}"
        output_file_slab="${output_dir_org_slab}/${workload}"
        output_file_dmesg="${output_dir_org_dmesg}/${workload}"

        dmesg -c > /dev/null 2>&1

        ${kevin_root_dir}/benchmark/setup_cheeze.sh
        ssh root@pt1 "cd /home/kukania/Koofs_proj/FlashFTLDriver/; ./cheeze_block_driver > /$(cat /etc/hostname)/$output_file_kukania 2>&1 < /dev/null" &
        while [ ! -f ${output_file_kukania} ]; do sleep 0.1; done
        tail -f ${output_file_kukania} | sed '/now waiting req/ q'
        now=$(date +"%T")
        echo "Current time : $now" >> ${output_file_perf}

        ${fs_sh}

        #sleep 2000
        flush
        sleep 5

        $kevin_root_dir/benchmark/general/blktrace.sh ${dev_path} ${output_file_cnt}
        iostat -c -d -x ${dev_path} 1 -m > ${output_file_stat} &
        vmstat 1 | gawk '{now=strftime("%Y-%m-%d %T "); print now $0}' > ${output_file_vmstat} &
        dmesg -w > ${output_file_dmesg} &

        sleep 5

        filebench -f ${workload} >> ${output_file_perf}

        #sleep 300
        df >> ${output_file_perf}

        sleep 30

        slabtop -o --sort=c > ${output_file_slab} 2>&1

        destroy

        ps -ef | grep blktrace | grep -v grep | awk '{print "kill -2 " $2}' | sh
        ps -ef | grep iostat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        ps -ef | grep vmstat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        while pgrep -f blktrace > /dev/null; do sleep 1; done

        rmmod cheeze

        ssh root@pt1 'kill -2 $(pgrep -fx ./cheeze_block_driver); while pgrep -fx ./cheeze_block_driver > /dev/null; do sleep 1; done'
        echo End workload
        sleep 5

        ps -ef | grep dmesg | grep -v grep | awk '{print "kill -15 " $2}' | sh
        while pgrep -f dmesg > /dev/null; do sleep 1; done
        dmesg -c > /dev/null 2>&1
    done
}

echo 0 > /proc/sys/kernel/randomize_va_space

#for test in ext4_metadata_journal ext4_data_journal xfs f2fs btrfs
for test in betrfs-nc betrfs-c
do
    output_dir_org_perf="$log_path/$test/perf"
    output_dir_org_cnt="$log_path/$test/trace"
    output_dir_org_kukania="$log_path/$test/kukania"
    output_dir_org_stat="$log_path/$test/iostat"
    output_dir_org_vmstat="$log_path/$test/vmstat"
    output_dir_org_slab="$log_path/$test/slab"
    output_dir_org_dmesg="$log_path/$test/dmesg"
    fs_sh="/mnt/home/koo/src/betrfs/${test}.sh"
    clean_sh="/mnt/home/koo/src/betrfs/${test}-clean.sh"
    do_ext4
done

#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

if [ -z "$1" ]; then
  echo "Please specify workload name for logging"
  exit 1
fi

log_path="/log/frag_filebench-kevin-$1-$(date +'%Y%m%d-%H%M%S')"
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

start_cheeze() {
    ssh root@pt1 "cd ${flash_driver_dir}/; ./koo_kv_driver -x -l 4 -c 3 -t 5 > /$(cat /etc/hostname)/$output_file_flashdriver 2>&1 < /dev/null" &
    while [ ! -f ${output_file_flashdriver} ]; do sleep 0.1; done
    tail -f ${output_file_flashdriver} | sed '/now waiting req/ q'

    cd ${kevin_root_dir}/kevinfs/
    ./run.sh ${target_dir}
    cd -
}

kill_cheeze() {
    rm -rf /tmp/filebench-shm-*
    sleep 60
    umount $target_dir
    sleep 10
    rmmod lightfs
    ssh root@pt1 'kill -2 $(pgrep -f '^./koo_kv_driver'); while pgrep -f '^./koo_kv_driver' > /dev/null; do sleep 1; done'
    sleep 5
}

# FlashDriver logs are saved at copy workload
setup_log() {
    echo "=============================================="
    echo ${workload}
    mkdir -p \
        "$output_dir_org_perf" \
        "$output_dir_org_vmstat" \
        "$output_dir_org_slab" \
        "$output_dir_org_dmesg"
    output_file_perf="${output_dir_org_perf}/${workload}"
    output_file_vmstat="${output_dir_org_vmstat}/${workload}"
    output_file_slab="${output_dir_org_slab}/${workload}"
    output_file_dmesg="${output_dir_org_dmesg}/${workload}"
}

frag() {
    filebench -f ${kevin_root_dir}/benchmark/fragmentation/frag_file.f
    profile=agrawal
    \time -v bash -c "${geriatrix_path}/geriatrix \
    -n $((128 * 1024 * 1024 * 1024)) \
    -u 0.4 \
    -r 42 \
    -m ${target_dir} \
    -a ${geriatrix_path}/profiles/$profile/age_distribution.txt \
    -s ${geriatrix_path}/profiles/$profile/size_distribution.txt \
    -d ${geriatrix_path}/profiles/$profile/dir_distribution.txt \
    -x /tmp/age.out -y /tmp/size.out -z /tmp/dir.out \
    -t 8 \
    -i 5 \
    -f 0 \
    -p 0 \
    -c 0 \
    -q 0 \
    -w 60 \
    -b posix"
}

fbench() {
    \time -v bash -c "filebench -f ${fload}"
}

run_bench() {
    #for workload in *.f
    #do
        dmesg -c > /dev/null 2>&1

        flush
        sleep 5

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

        ps -ef | grep vmstat | grep -v grep | awk '{print "kill -9 " $2}' | sh

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

#for test in ext4_metadata_journal ext4_data_journal xfs f2fs btrfs
for test in ext4_metadata_journal
do
    for fload in *.f
    do
        output_dir_org_perf="$log_path/$fload/perf"
        output_dir_org_vmstat="$log_path/$fload/vmstat"
        output_dir_org_slab="$log_path/$fload/slab"
        output_dir_org_dmesg="$log_path/$fload/dmesg"
        output_file_flashdriver="$log_path/$fload/flashdriver"

        setup_log

        start_cheeze

        workload=frag
        setup_log
        run_bench

        workload=fbench
        setup_log
        run_bench

        kill_cheeze
    done
done

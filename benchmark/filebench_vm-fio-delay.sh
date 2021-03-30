#!/bin/bash

if [ -z "$1" ]; then
  echo "Please specify workload name for logging"
  exit 1
fi

log_path="/log/filebench_vmfiolog-$1-$(date +'%Y%m%d-%H%M%S')"
dev_path="/dev/cheeze0"
target_dir="/bench"
udelay="$2"

mkdir -p $log_path

echo "Saving output to $log_path/totallog"

case "$3" in
    -f)
        ;;
    *)
        echo "Daemonizing script"
        $0 $1 $2 -f < /dev/null &> /dev/null & disown
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

start_cheeze() {
    ${kevin_root_dir}/real/setup_cheeze.sh
    echo $udelay > /sys/module/cheeze/parameters/delay_factor_ns
    ssh root@pt1 "cd /home/flashdriver/Koofs_proj/FlashFTLDriver/; export MS_TIME_SL=$udelay; chrt --rr 99 ./cheeze_block_driver > /$(cat /etc/hostname)/$output_file_flashdriver 2>&1 < /dev/null" &
    while [ ! -f ${output_file_flashdriver} ]; do sleep 0.1; done
    tail -f ${output_file_flashdriver} | sed '/now waiting req/ q'
    mkmount
}

kill_cheeze() {
    umount $target_dir
    rmmod cheeze
    ssh root@pt1 'kill -2 $(pgrep -fx ./cheeze_block_driver); while pgrep -fx ./cheeze_block_driver > /dev/null; do sleep 1; done'
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
        "$output_dir_org_slab" \
        "$output_dir_org_dmesg"
    output_file_perf="${output_dir_org_perf}/${alias}"
    output_file_cnt="${output_dir_org_cnt}/${alias}"
    output_file_flashdriver="${output_dir_org_flashdriver}/${alias}"
    output_file_stat="${output_dir_org_stat}/${alias}"
    output_file_vmstat="${output_dir_org_vmstat}/${alias}"
    output_file_slab="${output_dir_org_slab}/${alias}"
    output_file_dmesg="${output_dir_org_dmesg}/${alias}"
}

do_ext4() {
    #for workload in *.f
    #do
        now=$(date +"%T")
        echo "Current time : $now" >> ${output_file_perf}

        dmesg -c > /dev/null 2>&1

        #sleep 2000
        flush

        $kevin_root_dir/benchmark/blktrace.sh ${dev_path} ${output_file_cnt}
        iostat -c -d -x ${dev_path} 1 -m >> ${output_file_stat} &
        vmstat 1 | gawk '{now=strftime("%Y-%m-%d %T "); print now $0}' >> ${output_file_vmstat} &
        dmesg -w > ${output_file_dmesg} &

        sleep 5

        fio ${workload} >> ${output_file_perf} 2>&1

        slabtop -o --sort=c >> ${output_file_slab} 2>&1

        sync

        ps -ef | grep blktrace | grep -v grep | awk '{print "kill -2 " $2}' | sh
        ps -ef | grep iostat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        ps -ef | grep vmstat | grep -v grep | awk '{print "kill -9 " $2}' | sh
        while pgrep -f blktrace > /dev/null; do sleep 1; done
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

#ext4_data_journal xfs f2fs btrfs
#for test in ext4_data_journal xfs f2fs btrfs
for test in ext4_metadata_journal
do
    output_dir_org_perf="$log_path/$test/perf"
    output_dir_org_cnt="$log_path/$test/trace"
    output_dir_org_flashdriver="$log_path/$test/flashdriver"
    output_dir_org_stat="$log_path/$test/iostat"
    output_dir_org_vmstat="$log_path/$test/vmstat"
    output_dir_org_slab="$log_path/$test/slab"
    output_dir_org_dmesg="$log_path/$test/dmesg"
    fs_sh="${kevin_root_dir}/benchmark/$test.sh"

#    alias=seqwrite
#    setup_log
#    start_cheeze

#    workload=seqwrite.fio
#    do_ext4
#    flush

#    kill_cheeze
    alias=seqwrite_seqread
    setup_log
    start_cheeze

    workload=seqwrite.fio
    do_ext4
    flush

    workload=seqread.fio
    do_ext4
    flush

    kill_cheeze
done

exit 0
    alias=seqwrite_randread
    setup_log
    start_cheeze

    workload=seqwrite.fio
    do_ext4
    flush

    workload=randread.fio
    do_ext4
    flush

    kill_cheeze
    alias=randwrite
    setup_log
    start_cheeze

    workload=randwrite.fio
    do_ext4
    flush

    kill_cheeze
    alias=randwrite_seqread
    setup_log
    start_cheeze

    workload=randwrite.fio
    do_ext4
    flush

    workload=seqread.fio
    do_ext4
    flush

    kill_cheeze
    alias=randwrite_randread
    setup_log
    start_cheeze

    workload=randwrite.fio
    do_ext4
    flush

    workload=randread.fio
    do_ext4
    flush

    kill_cheeze
done

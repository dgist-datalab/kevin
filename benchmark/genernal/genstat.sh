#!/bin/bash

# Usage:
# cd ~/filebench_vmlog-20201015-010514
# ~/koofs/benchmark/genstats.sh

# set -eo pipefail

echo "Generating fs/iops.dat"
echo

ls -d */ | sort | while read fs; do
  cd $fs
  echo $fs

  ls perf/ | sort | while read workload; do
    echo -ne "$workload\t"
    cat perf/$workload | grep ' ops/s' | awk '{print $6}'
  done | sed \
  -e "s/micro_copyfiles.f/cp/g" \
  -e "s/micro_createfiles.f/creat_4K/g" \
  -e "s/micro_createfiles_empty.f/creat/g" \
  -e "s/micro_delete.f/unlink_4K/g" \
  -e "s/micro_delete_empty.f/unlink/g" \
  -e "s/micro_makedirs.f/mkdir/g" \
  -e "s/micro_removedirs.f/rmdir/g" \
  -e "s/real_fileserver.f/Fileserver/g" \
  -e "s/real_varmail.f/Varmail/g" \
  -e "s/real_webproxy.f/Webproxy/g" \
  -e "s/real_webserver.f/Webserver/g" | tee iops.dat

  echo "Saved to $(pwd)/iops.dat"

  cd ..
  echo
done

echo "Generating iops summary"

echo -ne '\t'
ls -d */ | sort | while read fs; do
  echo -ne "$fs\t"
done | sed \
  -e "s@btrfs/@BTRFS@g" \
  -e "s@xfs/@XFS@g" \
  -e "s@f2fs/@F2FS@g" \
  -e "s@ext4_data_journal/@EXT4-DATA@g" \
  -e "s@ext4_metadata_journal/@EXT4-META@g"
echo

LINE=$(wc -l $(ls $(ls -d */ | head -n1)/iops.dat) | awk '{print $1}')
for i in $(seq 1 $LINE); do
  awk "NR==$i{print \$1}" $(ls $(ls -d */ | head -n1)/iops.dat) | tr '\n' '\t'
  ls -d */ | sort | while read fs; do
    awk "NR==$i{print \$2}" $fs/iops.dat | tr '\n' '\t'
  done
  echo
done

echo "Generating fs/workload/kukasum"
echo

ls -d */ | sort | while read fs; do
  cd $fs
  echo $fs
  mkdir -p kukasum

  ls perf/ | sort | while read workload; do
    echo "Saving to $(pwd)/kukasum/$workload"
    (
      cat kukania/$workload | tail -n30 | grep '^MAPPING[RW] \|^DATA[RW] ' | tr ' ' '\t'
      echo -ne "GCR\t"
      echo $(cat kukania/$workload | tail -n30 | grep '^GCMR \|^GCDR \|^GCMR_DGC '| awk '{print $2}' | tr '\n' '+')0 | bc
      echo -ne "GCW\t"
      echo $(cat kukania/$workload | tail -n30 | grep '^GCMW \|^GCDW \|^GCMW_DGC '| awk '{print $2}' | tr '\n' '+')0 | bc
    ) | tee kukasum/$workload
    echo
  done

  cd ..
  echo
done

echo "Generating total kuka summary"

ls $(ls -d */ | head -n1)/kukasum/ | while read workload; do
  echo -ne "$workload"'\t'
  cat $(ls -d */ | head -n1)/kukasum/$workload | awk '{print $1}' | tr '\n' '\t'
  echo
  ls -d */ | sort | while read fs; do
    echo -ne "\"$fs\""'\t'
    LINE=$(wc -l $(ls $(ls -d */ | head -n1)/kukasum/$workload) | awk '{print $1}')
    for i in $(seq 1 $LINE); do
      awk "NR==$i{print \$2}" $fs/kukasum/$workload | tr '\n' '\t'
    done
    echo
  done
  echo
  echo
done | sed \
  -e "s@btrfs/@BTRFS@g" \
  -e "s@xfs/@XFS@g" \
  -e "s@f2fs/@F2FS@g" \
  -e "s@ext4_data_journal/@EXT4-DATA@g" \
  -e "s@ext4_metadata_journal/@EXT4-META@g" \
  -e "s/micro_copyfiles.f/cp/g" \
  -e "s/micro_createfiles.f/creat_4K/g" \
  -e "s/micro_createfiles_empty.f/creat/g" \
  -e "s/micro_delete.f/unlink_4K/g" \
  -e "s/micro_delete_empty.f/unlink/g" \
  -e "s/micro_makedirs.f/mkdir/g" \
  -e "s/micro_removedirs.f/rmdir/g" \
  -e "s/real_fileserver.f/Fileserver/g" \
  -e "s/real_varmail.f/Varmail/g" \
  -e "s/real_webproxy.f/Webproxy/g" \
  -e "s/real_webserver.f/Webserver/g"

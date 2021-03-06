#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

# Usage:
# cd ~/filebench_vmlog-20201015-010514
# ~/kevin/benchmark/genstats.sh

# set -eo pipefail

echo "Generating fs/workload/flashdriversum"
echo

ls -d */ | sort | while read fs; do
  cd $fs
  echo $fs

  echo "Saving to $(pwd)/flashdriversum"
  (
    cat flashdriver | tail -n30 | grep '^MAPPING[RW] \|^DATA[RW] ' | tr ' ' '\t'
    echo -ne "GCR\t"
    echo $(cat flashdriver | tail -n30 | grep '^GCMR \|^GCDR \|^GCMR_DGC '| awk '{print $2}' | tr '\n' '+')0 | bc
    echo -ne "GCW\t"
    echo $(cat flashdriver | tail -n30 | grep '^GCMW \|^GCDW \|^GCMW_DGC '| awk '{print $2}' | tr '\n' '+')0 | bc
  ) | tee flashdriversum
  echo

  cd ..
  echo
done

echo "Generating total flash driver summary"

echo -ne "$workload"'\t'
cat $(ls -d */ | head -n1)/flashdriversum | awk '{print $1}' | tr '\n' '\t'
echo
ls -d */ | sort | while read fs; do
  echo -ne "\"$fs\""'\t'
  LINE=$(wc -l $(ls $(ls -d */ | head -n1)/flashdriversum) | awk '{print $1}')
  for i in $(seq 1 $LINE); do
    awk "NR==$i{print \$2}" $fs/flashdriversum | tr '\n' '\t'
  done
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

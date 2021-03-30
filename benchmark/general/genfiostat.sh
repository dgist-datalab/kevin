#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

# Usage:
# cd ~/filebench_vmlog-20201015-010514
# ~/kevin/benchmark/genstats.sh

set -eo pipefail

echo "Generating fs/fio.dat"
echo

ls -d */ | sort | while read fs; do
  cd $fs
  echo $fs

  ls perf/ | sort | while read workload; do
    echo -ne "$workload\t"
    ls perf/$workload | while read a; do grep iB/s $a | tail -n1; done | sed -e 's/bw=//g' -e 's@MiB/s@@g' | awk '{print $2}'
  done | tee fio.dat

  cd ..
  echo
done

echo "Generating summary"

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

LINE=$(wc -l $(ls $(ls -d */ | head -n1)/fio.dat) | awk '{print $1}')
for i in $(seq 1 $LINE); do
  awk "NR==$i{print \$1}" $(ls $(ls -d */ | head -n1)/fio.dat) | tr '\n' '\t'
  ls -d */ | sort | while read fs; do
    awk "NR==$i{print \$2}" $fs/fio.dat | tr '\n' '\t'
  done
  echo
done

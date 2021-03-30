#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

# Usage:
# cd ~/filebench_vmlog-20201015-010514
# ~/kevin/benchmark/genstats.sh

set -eo pipefail

echo "Parsing trace"
echo

ls -d */ | sort | while read fs; do
  cd $fs

  if echo $fs | grep -qi xfs; then
    parser=parser_xfs
  else
    parser=parser_ext4
  fi

  mkdir -p parsed
  TARGET=cut/
  if [ ! -e $TARGET ]; then TARGET=trace/; fi
  echo "Using $TARGET"

  ls $TARGET | sort | while read workload; do
    ~/$parser $TARGET$workload > parsed/$workload &
  done &

  cd ..
done

while pgrep -f parser_ > /dev/null; do sleep 1; done

ls -d */ | sort | while read fs; do
  cd $fs

  mkdir -p iocount
  ls parsed/ | sort | while read workload; do
    echo -ne \"$fs\""\t" | sed \
      -e 's@btrfs@BTRFS@g' \
      -e 's@xfs@XFS@g' \
      -e 's@f2fs@F2FS@g' \
      -e 's@ext4_data_journal@EXT4-D@g' \
      -e 's@ext4_metadata_journal@EXT4-M@g' | tr -d '/' > iocount/$workload
    tail -n2 parsed/$workload | head -n1 | tr ' ' '\t' >> iocount/$workload
  done

  cd ..
done

ls $(ls -d */ | head -n1)/iocount/ | while read workload; do
  echo -e "$workload\tMDR\tMDW\tDR\tDW\tJ\tD"
  cat */iocount/$workload
  echo
  echo
done

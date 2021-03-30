#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

# Usage:
# cd ~/filebench_vmlog-20201015-010514
# ~/kevin/benchmark/genstats.sh

# set -eo pipefail

  (
    cat "$@" | tail -n30 | grep '^MAPPING[RW] \|^DATA[RW] ' | tr ' ' '\t'
    echo -ne "GCR\t"
    echo $(cat "$@" | tail -n30 | grep '^GCMR \|^GCDR \|^GCMR_DGC '| awk '{print $2}' | tr '\n' '+')0 | bc
    echo -ne "GCW\t"
    echo $(cat "$@" | tail -n30 | grep '^GCMW \|^GCDW \|^GCMW_DGC '| awk '{print $2}' | tr '\n' '+')0 | bc
  )
  echo

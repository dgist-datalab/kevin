#!/bin/bash

# Usage:
# cd ~/filebench_vmlog-20201015-010514
# ~/kevin/benchmark/cuttrace.sh

#set -eo pipefail

# Set from filebench_vm.sh
SLEEP=5

ls -d */ | while read fs; do
  cd $fs
  ls perf/ | while read workload; do
    echo $fs$workload

    TARGET=$SLEEP
    echo Target time: $TARGET

    TIME=$(awk "\$1 > $TARGET && \$1 < ($TARGET + 20)" trace/$workload | head -n1 | awk '{print $1}' || true)
    echo Got time: $TIME

    echo Variance: $(echo $TIME-$TARGET | bc)

    # Cut
    mkdir -p cut
    cat trace/$workload | sed -n -e "/$TIME/"',$p' | sed -e '/CPU/,$d' | awk '$1 < 100000' > cut/$workload

    echo
  done
  cd ..
done

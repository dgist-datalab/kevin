#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

cd delay_final
../filebench_vm-delay.sh koo-fb-factor-7200 7200 -f
../filebench_vm-delay.sh koo-fb-factor-3200 3200 -f
../filebench_vm-delay.sh koo-fb-factor-1350 1350 -f
../filebench_vm-delay.sh koo-fb-factor-0 0 -f

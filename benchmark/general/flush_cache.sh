#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

sync
echo 3 > /proc/sys/vm/drop_caches

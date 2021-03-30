#!/bin/bash

if [[ "$kevin_env_set" != "1" ]]; then
  echo "Please source env for kevin"
  exit 1
fi

echo
echo Running micro
echo

cd workloads_final
../filebench_blk.sh
cd ..

echo
echo Running fio
echo

cd fio
../filebench_blk-fio.sh
cd ..

echo Done

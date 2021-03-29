#!/bin/bash

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

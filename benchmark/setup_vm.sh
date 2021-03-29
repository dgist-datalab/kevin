#!/bin/bash

set -eo pipefail

rm -f /dev/hugepages/qemutest
fallocate -l 4G /dev/hugepages/qemutest

# Set addresses
TMP=/tmp/setup_vm
dmesg -c | grep 'hugetlbfs: qemutest: index ' | tail -n4 | tee $TMP

./setup_cheeze.sh

# Turn on VM
sync
echo "Turning on VM"
virsh start passthru

echo "Waiting for VM to come online"
until timeout 0.5 ssh root@pt1 true 2>/dev/null; do sleep 0.5; done
echo "VM online"

# Setup VCU
ssh root@pt1 'sync; /home/kukania/amf-driver/programFPGA/program-vcu108.sh; sync' || true

echo "VM setup done"

#!/bin/bash

set -eo pipefail

fallocate -l 4G /dev/hugepages/qemutest

# Set addresses
TMP=/tmp/setup_vm
dmesg -c | grep 'hugetlbfs: qemutest: index ' | tail -n4 | tee $TMP

# Turn on VM
sync
echo "Turning on VM"
virsh start passthru

echo "Waiting for VM to come online"
until timeout 0.5 ssh root@10.150.21.48 true 2>/dev/null; do sleep 0.5; done
echo "VM online"

# Setup VCU
ssh root@10.150.21.48 'sync; /home/kukania/amf-driver/programFPGA/program-vcu108.sh; sync' || true

echo "VM setup done"

cat /tmp/setup_vm

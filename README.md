# KEVIN

## What is KEVIN?

KEVIN is a fast, lightweight & POSIX-compliant file system with a key-value storage device that performs in-storage indexing.

KEVIN is composed of two main elements: **KevinSSD**(providing key-value interface with in-storage indexing), **KevinFS**(providing file system abstraction)

The original paper that introduced KEVIN is currently in the revision stage of [USENIX OSDI 2021](https://www.usenix.org/conference/osdi21).

The archive snapshot with a detailed screencast used in the paper is available at [![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.4659803.svg)](https://doi.org/10.5281/zenodo.4659803).



## Prerequisites

* KevinFS is implemented in Linux kernel 4.15.18. The kernel source is provided below.

* KevinSSD is currently built on special hardware(Xilinx VCU108+Customized NAND Flash).
  * VCU board is connected to host with PCIe interface.
  * KevinSSD runs on virtual machine(QEMU) and communicates to KevinFS with shared memory(w/ hugepage).
  * To run KevinSSD, Xilinx Vivado 2018.1 must be pre-installed on the VM.

Source code:

* [KevinSSD](https://github.com/dgist-datalab/KevinSSD)

* KevinFS is contained in this repository(`./kevin/kevinfs`).

* [BlockSSD](https://github.com/dgist-datalab/BlockSSD): Block device implemented with PageFTL for comparison.

* [Kernel](https://github.com/dgist-datalab/linux/tree/kevin-4.15): Kernel source used with Kevin.

  

## Installation & Execution

### Acquiring source code & preparation

* Clone required repository(KevinFS) into your host machine.

```
$ git clone https://github.com/dgist-datalab/kevin
```

* On the host machine, add `default_hugepagese=1G hugepagesz=1G hugepage=236 intel_iommu=on nopat` to `GRUB_CMDLINE_LINUX_DEFAULT` in GRUB configuration file(located by default in `/etc/default/grub`).

  > Currently, KEVIN uses hugepage as the communicator between KevinFS and KevinSSD.

* To set up the VM(KevinSSD, BlockSSD) environment, modify `libvirt/passthru.xml` adequately and use `virsh define libvirt/passthru.xml`. Then run the VM:

  ```
  # cd ~/kevin; . ./env; cd benchmark; ./setup_vm.sh
  ```

* On the VM, clone required repositories(BlockSSD, KevinSSD).

  ```
  $ git clone https://github.com/dgist-datalab/KevinSSD
  $ git clone https://github.com/dgist-datalab/BlockSSD
  ```

* On the VM, run following command and paste the result to `~/BlockSSD/interface/cheeze_hg_block.c:16` and `~/KevinSSD/interface/koo_hg_inf.c:24`.

  ```
  $ echo 0x$(sudo lspci -vvv | grep -A6 Inter-VM | grep size=4G | awk '{print $5}')
  ```

* To run KevinFS successfully, you should modify `~/kevin/env` to match it to your workspace layout.

### KevinSSD (on VM)

0. Initialize the FPGA board.

   ```
   $ git clone https://github.com/chanwooc/amf-driver.git; cd amf-driver/programFPGA
   $ ./program-vcu108.sh
   ```

1. Install required packages as following:

   ```
   $ sudo apt install libjemalloc-dev
   ```

2. Build the binary.

   ```
   $ cd ~/KevinSSD; make
   ```

3. Run! 

   > Note: If you don't have the hardware stated above, this process cannot succeed.
   >
   > To shutdown the KevinSSD, just hit *Ctrl+C*.

   ```
   $ sudo ./koo_kv_driver -x -l 4 -c 3 -t 5
   ```

### KevinFS (on host machine)

KevinFS can only be built & executed in Linux kernel 4.15.18.

0. Run following command on the host machine.

   > Following result is just an example. Virtual values will vary across systems.

   ```
   $ cat /tmp/setup_vm
   [  169.796239] hugetlbfs: qemutest: index 0: 0x3f00000000
   [  169.863023] hugetlbfs: qemutest: index 1: 0x3ec0000000
   [  169.921743] hugetlbfs: qemutest: index 2: 0x3e80000000
   [  169.980463] hugetlbfs: qemutest: index 3: 0x3e40000000
   ```

   Copy & paste the memory addresses (e.g. `0x3f00000000`) of index `0,1,2` into line 35, 36, 37 of `~/kevin/kevinfs/cheeze/cheeze.h`.

1. Build the KevinFS binary.

   ```
   $ cd ~/kevin/kevinfs; make
   ```

2. Mount the file system.

   ```
   # rmmod cheeze 2>/dev/null # Remove conflicting module
   # ./run.sh /bench
   ```

3. KevinFS will be now available at `/bench`.




## Benchmark
Before running the benchmark scripts, you should <u>shutdown KevinSSD & KevinFS</u>. Following scripts will automatically build and destroy them, to guarantee a consistent result.

* To run example benchmark(part of filebench):

  * This command takes only **5 hours** to run in total.
  * run:
    - kevin (<30 mins to run): `cd benchmark/filebench/example; ../filebench_vm_kevin.sh {test_name}`
    - others (<270 mins to run): `cd benchmark/filebench/example; ./filebench_vm.sh {test_name}`
  * Results will be saved in a directory called `/log/{benchmark_name}_{test_name}-{date}`.

* To run all scripts in directory `~/kevin/benchmark` to replicate the full experiments in the paper
  - This command takes **2 week** to run 1 repeat of all benchmarks for all filesystems (we should run across 5 file systems).
  - filebench: 
    - kevin: `cd benchmark/filebench/workloads; ../filebench_vm_kevin.sh {test_name}`
    - others: `cd benchmark/filebench/workloads; ../filebench_vm.sh {test_name}`
  - fio:
    - kevin: `cd benchmark/fio; ./fio_kevin.sh {test_name}`
    - others: `cd benchmark/fio; ./fio.sh {test_name}`
  - aged filesystem:
    - kevin: `cd benchmark/fragmentation/workloads; ../frag_filebench_geriatrix-kevin.sh {test_name}`
    - others: `cd benchmark/fragmentation/workloads; ../frag_filebench_geriatrix.sh {test_name}`
  - applications(linux):
    - kevin: `cd benchmark/application/linux/kevin; ./linux.sh; ./rsync.sh {test_name}`
    - others: `cd benchmark/application/linux; ./linux.sh; ./rsync.sh {test_name}`
  - applications(tpcc):
    - kevin: `cd benchmark/application/tpcc/kevin; ./tpcc.sh {test_name}`
    - others: `cd benchmark/application/tpcc; ./tpcc.sh {test_name}`
  - Results will be saved in a directory called `/log/{benchmark_name}_{test_name}-{date}`.
  
* Log structure:

  ```
  totallog: Stdout/stderr of the benchmark script
  flashdriver: The log of SSD simulator
  slab: Slab information of the kernel
  dmesg: Kernel log
  vmstat: Polled vmstat log
  perf: Stdout of benchmark softwares(e.g. filebench)
  ```


* We used [Geriatrix](https://github.com/saurabhkadekodi/geriatrix) for file system aging, to simulate fragmentation.
* You may tune some parameters to reproduce the results in the paper.


# ReFlex4ARM

ReFlex4ARM is a variant of ReFlex(https://github.com/stanford-mast/reflex). ReFlex uses Intel [DPDK](http://dpdk.org) for fast packet processing and Intel [SPDK](http://www.spdk.io) for high performance access to NVMe Flash. This is an improved implementation of ReFlex, as presented in the [paper](https://web.stanford.edu/group/mast/cgi-bin/drupal/system/files/reflex_asplos17.pdf) published at ASPLOS'17.

ReFlex is a software-based system that provides remote access to Flash with performance nearly identical to local Flash access. ReFlex closely integrates network and storage processing in a dataplane  kernel to achieve low latency and high throughput at low resource requirements. ReFlex uses a novel I/O scheduler to enforce tail latency and throughput service-level objectives (SLOs) for multiple clients sharing a remote Flash device.

ReFlex4ARM extends the original implementation of ReFlex that was used in the Intel-based platform to an ARM-based platform. With an energy-efficient yet performant Broadcom's Stingray Platform, we are able to offload all the previous tasks to the smart NIC and free the host entirely. The goal of Reflex4ARM is to build an integrated storage, computation and network platform with prior ReFlex and find a new tradeoff among these three. This NIC has 8 ARM A72 cores and it is so tiny and low-powered that using it as a PCI-e root complex with a fanout of several SSDs would largely lower down the current OPEX in data centers. Without degrading much of the performance, we can come up with a flash disaggregation solution with much lower footprint and TCO.

## What's new?

* More portable codes across multiple DPDK versions and architectures (especially for ARM).
* TCP parameters tuned for delivering most performance at 100Gbps.
* Client API support for tenants to dynamically register SLOs. 
* Support for multiple NVMe device management by a single ReFlex server.


## Requirements for ARM version of ReFlex

ReFlex4ARM requires at least a NVMe Flash device and a [smart NIC from Broadcom](https://www.broadcom.com/products/ethernet-connectivity/smartnics) on the storage target. The client networking interface card should have at least equivalant computing and networking ability. You might also need to download Broadcom's proprietary DPDK library to compile. 

Our experiment environments:

Server-end:

* Broadcom Stingray SST100 Board
* Octal 3 GHz ARM A72 cores
* 4 Samsung 970 Evo NVMe SSDs
* Memory size: 3*16 GB
* Ubuntu 16.04 LTS (kernel 4.14.79)
* DPDK/SPDK: v17.11/v18.04

Client-end:

* NIC: Broadcom P1100p 1x100G NIC
* Intel(R) Core(TM) i7-7700 CPU @ 3.60GHz
* Memory size: 2*8 GB
* Ubuntu 17.10 (kernel 5.0.7)
* DPDK/SPDK: v19.05/v19.04


## AWS EC2 Environments

x86_64 Server:

* Image: Ubuntu Server 20.04 LTS (ami-06e54d05255faf8f6)
* Instance: i3-large or i3-xlarge or i3-2xlarge
* Network Interface: 2 * Elastic Network Adaptor (ENA)

ARM Server: TBD

## Setup Instructions


1. Download source code and fetch dependencies:

   ```
   git clone https://github.com/mhxie/reflex4arm.git
   cd reflex4arm
   git checkout meson
   ./deps/fetch-deps.sh # on stingray, change the dpdk and spdk address in deps/DEPS
   ```

2. Install library dependencies: 

   ```
   sudo apt-get update
   sudo apt-get install libconfig-dev libnuma-dev libpciaccess-dev libaio-dev libevent-dev g++-multilib libcunit1-dev libssl-dev uuid-dev python3-pip net-tools
   sudo pip3 install meson ninja
   sudo ./deps/spdk/scripts/pkgdep.sh
   ```

3. Build the dependecies:

   ```
   # Build DPDK
   cd deps/dpdk
   enable_kmods 'true' # enable igb_uio build option in meson_options.txt
   meson build
   meson compile -C build
   mkdir install
   DESTDIR=`pwd`/install meson install -C build
   # Patch DPDK build dir for SPDK
   DPDK_INSTALL_LIB=`pwd`/install/usr/local/lib
   mv $DPDK_INSTALL_LIB/x86_64-linux-gnu/* $DPDK_INSTALL_LIB
   # Build SPDK
   cd ../spdk
   git submodule update --init
   ./configure --with-dpdk=../dpdk/install/usr/local
   make
   cd ../.. 	
   ```

4. Build ReFlex4ARM:
   <!-- PKG_CONFIG_PATH=$DPDK_INSTALL_LIB/pkgconfig meson build -->

   ```
   // INSTALL_PATH=`pwd`
   // meson -Dprefix=$INSTALL_PATH build
   meson build
   meson compile -C build
   // meson install -C build

   # if you want to clean the build
   rm -r $INSTALL_PATH/build
   // rm -r $INSTALL_PATH/bin
   ```

5. Set up the environment:

   ```
   cp ix.conf.sample ix.conf
   # modify at least host_addr, gateway_addr, devices, flow director, and nvme_devices (ns_size at clients)
  
   sudo modprobe uio
   IGB_UIO_PATH=deps/dpdk/build/kernel/linux/igb_uio/igb_uio.ko
   sudo insmod $IGB_UIO_PATH wc_active=1 # spdk setup will do this for you also
   grep PCI_SLOT_NAME /sys/class/net/*/device/uevent | awk -F '=' '{print $2}'
   sudo deps/dpdk/usertools/dpdk-devbind.py --bind=igb_uio 0000:00:04.0 # insert device PCI address here!!! 

   sudo modprobe -r nvme
   sudo -E DRIVER_OVERRIDE=$IGB_UIO_PATH deps/spdk/scripts/setup.sh
   
   sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'
   ```
   
6. Precondition the SSD and derive the request cost model:

   It is necessary to precondition the SSD to acheive steady-state performance and reproducible results across runs. We recommend preconditioning the SSD with the following local Flash tests: write *sequentially* to the entire address space using 128KB requests, then write *randomly* to the device with 4KB requests until you see performance reach steady state. The time for the random write test depends on the capacity, use `./perf -h` to check how to configure. 
   ```
   cd deps/spdk/examples/nvme/perf
   sudo ./perf -q 1 -s 131072 -w write -t 3600 -c 0x1 -r 'trtype:PCIe traddr:0001:01:00.0'
   ```
   
   After preconditioning, you should derive the request cost model for your SSD:

   ```
   cp sample.devmodel nvme_devname.devmodel
   # update request costs and token limits to match your SSD performance 
   # see instructions in comments of file sample.devmodel
	# in ix.conf file, update nvme_device_model=nvme_devname.devmodel
   ```
   You may use any I/O load generation tool (e.g. [fio](https://github.com/axboe/fio)) for preconditioning and request calibration tests. Note that if you use a Linux-based tool, you will need to reload the nvme kernel module for these tests (remember to unload it before running the ReFlex server). 
  
   For your convenience, we provide an open-loop, local Flash load generator based on the SPDK perf example application [here](https://github.com/anakli/spdk_perf). We modified the SPDK perf example application to report read and write percentile latencies. We also made the load generator open-loop, so you can sweep throughput by specifying a target IOPS instead of queue depth. See setup instructions for ReFlex users in the repository's [README](https://github.com/anakli/spdk_perf/blob/master/README.md).


## Running ReFlex4ARM

### 0. Change some kernel settings (only at stingray):
   ```
   sudo vim /etc/security/limits.conf

   *               hard    memlock         unlimited
   *               soft    memlock         unlimited
   ```

### 1. Run the ReFlex4ARM server:

   ```
   sudo ./build/dp
   ```

   ReFlex runs one dataplane thread per CPU core. If you want to run multiple ReFlex threads (to support higher throughput), set the `cpu` list in ix.conf and add `fdir` rules to steer traffic identified by {dest IP, src IP, dest port} to a particular core.

#### Registering service level objectives (SLOs) for ReFlex tenants:

* A *tenant* is a logical abstraction for accounting for and enforcing SLOs. ReFlex4ARM supports two types of tenants: latency-critical (LC) and best-effort (BE) tenants. 
* The current implementation of ReFlex4ARM follows the original design of ReFlex4ARM, which requires tenant SLOs to be specified statically (before running ReFlex) in `pp_accept()` in `dp/core/reflex_server.c`. Each port ReFlex4ARM listens on can be associated with a separate SLO. The tenant should communicate with ReFlex using the destination port that corresponds to the appropriate SLO. This is a temporary implementation until there is proper client API support for a tenant to dynamically register SLOs with ReFlex. 


### 2. Run a ReFlex client:

There are several options for clients in the original ReFlex implementations,

#### 2.1 Run an IX-based client that opens TCP connections to ReFlex and sends read/write requests to logical blocks.
   	
   Clone ReFlex4ARM source code (userspace branch) on client machine and follow steps 1 to 6 in the setup instructions in userspace branch README. Change the target from `arm64-native-linuxapp-gcc` to `x86_64-native-linuxapp-gcc`. Comment out `nvme_devices` in ix.conf. 
   
   Single-thread test example: 

   ```
   # example: sudo ./build/dp -s 10.10.66.3 -p 1234 -w rand -T 1 -i 100000 -r 100 -S 0 -R 4096 -P 0 
   ```

   Multiple-thread test example:
   ```
   # example: sudo ./build/dp -s 10.10.66.3 -p 1234 -w rand -T 4 -i 400000 -r 100 -S 0 -R 4096 -P 0
   ```
   
   More descriptions for the command line options can be found by running
   ```
   sudo ./build/dp -h
   
   Usage: 
   sudo ./ix
   to run ReFlex server, no parameters required
   to run ReFlex client, set the following options
   -s  server IP address
   -p  server port number
   -w  workload type [seq/rand] (default=rand)
   -T  number of threads (default=1)
   -i  target IOPS for open-loop test (default=50000)
   -r  percentage of read requests (default=100)
   -S  sweep multiple target IOPS for open-loop test (default=1)
   -R  request size in bytes (default=1024)
   -P  precondition (default=0)
   -d  queue depth for closed-loop test (default=0)
   -t  execution time for closed-loop test (default=0)
   ```



   Sample output:

   ```
   RqIOPS:  IOPS:   Avg:    10th:   20th:   30th:   40th:   50th:   60th:   70th:     80th:   90th:   95th:   99th:   max:    missed:
   600000   599905  155     106     112     119     126     135     150     167       190     232     274     377     1583    202947
   RqIOPS:  IOPS:   Avg:    10th:   20th:   30th:   40th:   50th:   60th:   70th:     80th:   90th:   95th:   99th:   max:    missed:
   600000   599913  154     104     111     117     124     133     148     165       189     231     273     378     1578    193293
   ```

#### 2.2 Run a legacy client application using the ReFlex remote block device driver [WIP].

This client option is provided to support legacy applications. ReFlex exposes a standard Linux remote block device interface to the client (which appears to the client as a local block device). The client can mount a filesystem on the block device. With this approach, the client is subject to overheads in the Linux filesystem, block storage layer and network stack.
On the client, change into the reflex_nbd directory and type make. Be sure that the remote ReFlex server is running and that it is ping-able from the client. Load the reflex.ko module, type dmesg to check whether the driver was successfully loaded. To reproduce the fio results from the paper do the following.

*Caveat: This is only for bdev-based tests without file system*

Before begining your tests, change the following parameters at reflex_nbd/reflex_nbd.c:
* `dest_addr` to the destination ip
* `REFLEX_SIZEBYTES` to the `ns_size`
* `dest_cpunr` to the number of threads running at your server
* `submit_queues` to the number of threads running at your client

```
cd reflex_nbd
make

sudo modprobe bnxt_en
 # or try sudo python deps/dpdk/usertools/dpdk-devbind.py --bind=bnxt_en 01:00.0 && sudo rmmod igb_uio
sudo modprobe nvme
 # make sure you have started reflex_server on the server machine and can ping it

sudo insmod reflex.ko
sudo apt install fio
for i in 1 2 4 8 16 32 ; do BLKSIZE=4k DEPTH=$i fio randread_remote.fio; done
```

##  Networking Tuning Guide

Depending on which workload you are using, we suggest the following changes to get the most of the performance out:

### Read-heavy workloads with small request sizes (1k/4k)
   1. For client:
   - `inc/lwip/lwipopts.h`: TCP_SND_BUF = 4096; TCP_WND = 1 << 16;
   - `libix/ixev.h`: IXEV_SEND_DEPTH = 16 * 2
   2. For server:
   - `inc/lwip/lwipopts.h`: TCP_SND_BUF = 65536 * 2; TCP_WND = 1 << 15;
   - `libix/ixev.h`: IXEV_SEND_DEPTH = 16 * 1
   
### Read-heavy workloads with huge request sizes (64k/128k)
   1. For client:
   - `inc/lwip/lwipopts.h`: TCP_SND_BUF = 4096; TCP_WND = 1 << 16;
   - `libix/ixev.h`: IXEV_SEND_DEPTH = 16 * 1
   2. For server:
   - `inc/lwip/lwipopts.h`: TCP_SND_BUF = (65536+24) * 8; TCP_WND = 1 << 15;
   - `libix/ixev.h`: IXEV_SEND_DEPTH = 16 * 1

Please also check the TCP tuning at [lwIP wiki](https://lwip.fandom.com/wiki/Tuning_TCP).


## Reference

Please refer to the original [implementations](https://github.com/stanford-mast/reflex) of ReFlex and its [paper](https://web.stanford.edu/group/mast/cgi-bin/drupal/system/files/reflex_asplos17.pdf):

Ana Klimovic, Heiner Litz, Christos Kozyrakis
ReFlex: Remote Flash == Local Flash
in the 22nd International Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS'22), 2017

## TODO

1. Update the lwIP to the latest version;
2. Adaptive batching for 100GbE networking;

## Future work

The ARM-based implementation of ReFlex is for the ReFlex server. ReFlex provides an efficient *dataplane* for remote access to Flash. To deploy ReFlex in a datacenter cluster, ReFlex should be combined with a control plane to manage Flash resources across machines and optimize the allocation of Flash IOPS and capacity. We are implementing a control plane with job-level scheduler in the new branch.


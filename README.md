# AEP-DSM

AEP-DSM is a research prototype built around Atomic Execution Protection (AEP),
as described in the AEP paper. AEP provides an isolated private execution space
for code and data that should remain protected from the ordinary application
address space. This repository uses that execution model to host a distributed
shared-memory allocator: the allocator is loaded into the AEP space, exposes a
single shadow-entry ABI, and test programs issue allocation, reference, message,
and ownership-transfer requests through that entry.

The allocator implements a two-level memory-management design. Level 1 manages
client-local allocation and reference metadata, while Level 2 manages node-level
and cross-node memory ownership. The tests under `tests/` exercise allocator
throughput, key-value access, ownership transfer, serverless-style chain
passing, and PageRank-style growing working sets.

## Repository Layout

```text
linux-6.9.5/          Linux 6.9.5 tree with AEP kernel changes
musl-for-aep/         AEP-musl source/build directory; expected to produce obj/musl-gcc
loader/               Position-independent AEP loader; builds pv_loader
allocator/            AEP-DSM allocator and shadow-entry implementation
tests/aep_dsm_client/ Shared client ABI used by all tests
tests/threadtest/     Multithreaded malloc/free benchmark
tests/kv/             Multithreaded key-value benchmark
tests/transfer_bench/ Single-object and binary-tree transfer benchmark
tests/serverless_chain/ Ownership chain benchmark
tests/pagerank/       Growing-working-set PageRank benchmark
```

## Build Overview

The full AEP path is:

1. Build the AEP-enabled kernel.
2. Boot into the AEP kernel.
3. Build and install AEP-musl.
4. Build the AEP loader.
5. Build the allocator with AEP enabled.
6. Build and run tests with AEP-musl; test requests enter the allocator through
   the shadow entry.

A convenience direct-link mode is also available for allocator debugging. In
that mode tests link `allocator/libaepmalloc.a` directly and do not require the
AEP loader or shadow-entry mapping.

## 1. Build the AEP Kernel

The kernel tree is in `linux-6.9.5/`. Configure the kernel with AEP enabled:

```sh
cd linux-6.9.5
make debian_defconfig
./scripts/config --enable ATOMIC_EXECUTION_PROTECTION
make olddefconfig
```

Then build the kernel:

```sh
make -j$(nproc)
```

The repository also provides a helper script:

```sh
cd linux-6.9.5
./run.sh build_kernel
```

If the helper regenerates the default configuration, re-check that
`CONFIG_ATOMIC_EXECUTION_PROTECTION=y` is present in `.config` before relying on
that image.

## 2. Install and Boot the AEP Kernel

For the QEMU rootfs flow bundled with the kernel tree, install the kernel,
modules, and headers into the rootfs image:

```sh
cd linux-6.9.5
sudo ./run.sh build_rootfs      # create ubuntu.ext4 if it does not exist
sudo ./run.sh update_rootfs     # install the current kernel/modules/headers
```

Boot the AEP kernel:

```sh
cd linux-6.9.5
./run.sh run
```

A NUMA-oriented launch mode is also present:

```sh
cd linux-6.9.5
./run.sh numa
```

## 3. Build and Install AEP-musl

The AEP-musl source tree and build output must live in `musl-for-aep/`, and the
compiler wrapper must exist at `musl-for-aep/obj/musl-gcc` before AEP builds are
attempted.

A typical musl build shape is:

```sh
cd musl-for-aep
./configure --prefix=/usr/local/aep-musl
make -j$(nproc)
sudo make install
```

After the build, verify the compiler wrapper expected by this repository:

```sh
test -x obj/musl-gcc
./obj/musl-gcc --version
```

Inside the AEP kernel environment, install AEP-musl into the target rootfs or
make the `musl-for-aep/obj/musl-gcc` path available through the repository mount.

## 4. Build the AEP Loader

The loader lives in `loader/` and builds `pv_loader` by default:

```sh
cd loader
make clean
make
```

The loader is position-independent and is used to place the protected allocator
image into the AEP execution space.

## 5. Build the Allocator for AEP

Production AEP build:

```sh
cd allocator
make clean
make AEP=1
```

## 6. Load the Allocator into AEP Space

After building `loader/pv_loader` and `allocator/aep-malloc`, use the loader in
the AEP kernel environment to map the allocator image into the protected AEP
address range. Tests use the default shadow-entry address from
`tests/aep_dsm_client/aep_dsm_client.h`; pass `--entry=0xaddr` only if your setup
uses a different entry address.

## 7. Build and Run Tests with AEP-musl

Each benchmark includes `tests/aep_dsm_client/aep_dsm_client.h`. In the normal
AEP path, the test binary does not link allocator objects; it calls the allocator
through the shadow-entry function pointer.

Build tests with AEP-musl by overriding `CC` from each test directory:

```sh
cd tests/threadtest
make clean
make CC=../../musl-for-aep/obj/musl-gcc
./threadtest 16 1000 100000 0 8
```

Other benchmarks follow the same pattern:

```sh
cd tests/kv
make clean
make CC=../../musl-for-aep/obj/musl-gcc
./kv 16 9 1000000

cd ../transfer_bench
make clean
make CC=../../musl-for-aep/obj/musl-gcc
./transfer_bench 100000 64 10 --mode=all

cd ../serverless_chain
make clean
make CC=../../musl-for-aep/obj/musl-gcc
./serverless_chain 100000 64 10

cd ../pagerank
make clean
make CC=../../musl-for-aep/obj/musl-gcc
./pagerank 4 4096 4 5 8
```

Most tests also accept:

```text
--entry=0xaddr   Override the shadow-entry address
--skip-init      Skip allocator initialization when it has already been done
```

See each test's local README for its exact command-line arguments.

## 8. Direct-Link Debugging Mode

For allocator development, tests can bypass AEP and link the allocator library
directly:

```sh
cd tests/threadtest
make clean
make DIRECT_LINK=1
./threadtest 16 1000 100000 0 8
```

This mode is useful for fast allocator debugging, but it is not a replacement
for the full AEP path because it does not exercise the loader or shadow-entry
mapping.

## QEMU Support

The repository includes a QEMU helper at `linux-6.9.5/run.sh`. It can boot the
AEP kernel, attach the bundled rootfs image, and emulate a CXL memory device so
that the AEP CXL mapping path can be validated inside a guest.

Prepare the image first:

```sh
cd linux-6.9.5
sudo ./run.sh build_rootfs
sudo ./run.sh update_rootfs
```

Then boot the guest:

```sh
cd linux-6.9.5
./run.sh run
```

If the local QEMU binary or firmware path differs from your machine defaults,
override them with environment variables before launching:

```sh
QEMU_BIN=/path/to/qemu-system-x86_64 \
QEMU_EXTRA_OPTS="-L /path/to/qemu/firmware" \
./run.sh run
```

Inside the guest, build the CXL map test and prepare the emulated CXL device:

```sh
cd /mount/of/aep-dsm/tests/cxl_map
make

cd /mount/of/aep-dsm/tests
sudo ./setup_cxl_dax.sh
```

The helper prints the physical base of `/dev/dax0.0` as `PHYS=0x...`. Use that
address with the CXL map test:

```sh
cd /mount/of/aep-dsm/tests/cxl_map
sudo ./cxl_map --phys=0xPHYS --size=0x1000 --rw-test
```

The expected success signal is:

```text
rw-test: PASS
```

`tests/setup_cxl_dax.sh` is idempotent for repeated guest setup. It loads the
required CXL, NVDIMM, and DAX modules, creates `region0` when needed, creates a
devdax namespace when needed, and reports the resource file backing
`/dev/dax0.0`.

## Ubuntu 24.04 Rootfs

For the QEMU path, an Ubuntu 24.04 rootfs is often more convenient than the
older prebuilt image when validating CXL support. In this repository it is the
easier base for guest-side CXL simulation because the required user-space stack
for CXL region and devdax management can be installed directly from the distro,
including `ndctl`, `daxctl`, and the `cxl` toolchain used by the tests.

The repository provides a helper script:

```sh
cd linux-6.9.5
sudo ./make_ubuntu2404_rootfs.sh
```

The script uses `debootstrap` to create an Ubuntu 24.04 (`noble`) rootfs in
`linux-6.9.5/rootfs_debian_x86_64`, installs the packages needed for serial
login and CXL testing, enables `ttyS0` root autologin, and, when a kernel image
is already available, invokes `./run.sh build_rootfs` to pack the rootfs into
`linux-6.9.5/ubuntu.ext4`.

Useful environment overrides:

```sh
UBUNTU_MIRROR=http://archive.ubuntu.com/ubuntu ROOT_PASSWORD=root FORCE_REBUILD_ROOTFS=1 FORCE_REBUILD_IMAGE=1 sudo ./make_ubuntu2404_rootfs.sh
```

After the image is created, continue with the normal QEMU flow:

```sh
cd linux-6.9.5
sudo ./run.sh update_rootfs
./run.sh run
```

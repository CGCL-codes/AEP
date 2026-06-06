#!/bin/bash

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

LROOT=$SCRIPT_DIR
RROOT=$SCRIPT_DIR
JOBCOUNT=${JOBCOUNT=$(nproc)}
QEMU_BIN=${QEMU_BIN:-qemu-system-x86_64}
CXL_MEM_PATH=${CXL_MEM_PATH:-/tmp/cxltest.raw}
CXL_LSA_PATH=${CXL_LSA_PATH:-/tmp/lsa.raw}
read -r -a QEMU_EXTRA_OPTS <<< "${QEMU_EXTRA_OPTS:-}"
if [ -z "${KERNEL_DEBUG_OPTS+x}" ]; then
	KERNEL_DEBUG_OPTS="cxl_acpi.dyndbg=+fplm cxl_pci.dyndbg=+fplm cxl_core.dyndbg=+fplm cxl_mem.dyndbg=+fplm cxl_pmem.dyndbg=+fplm cxl_port.dyndbg=+fplm cxl_region.dyndbg=+fplm cxl_test.dyndbg=+fplm cxl_mock.dyndbg=+fplm cxl_mock_mem.dyndbg=+fplm dax.dyndbg=+fplm dax_cxl.dyndbg=+fplm device_dax.dyndbg=+fplm"
fi
KERNEL_EXTRA_APPEND=${KERNEL_EXTRA_APPEND:-}
if [ -z "${QEMU_NET_OPTS+x}" ]; then
	QEMU_NET_OPTS=(-netdev user,id=mynet -device virtio-net-pci,netdev=mynet)
else
	read -r -a QEMU_NET_OPTS <<< "$QEMU_NET_OPTS"
fi
cd "$LROOT"
export ARCH=x86_64
export INSTALL_PATH=$LROOT/rootfs_debian_x86_64/boot/
export INSTALL_MOD_PATH=$LROOT/rootfs_debian_x86_64/
export INSTALL_HDR_PATH=$LROOT/rootfs_debian_x86_64/usr/

kernel_build=$LROOT/rootfs_debian_x86_64/usr/src/linux/
rootfs_path=$LROOT/rootfs_debian_x86_64
rootfs_image=${ROOTFS_IMAGE:-$RROOT/ubuntu.ext4}
#rootfs_debian_x86_64
rootfs_size=8192

SMP="-smp 10"

NUMA_SMP="-smp 8,sockets=2,cores=4,threads=1"  # 设置CPU参数，8个vCPU，2个插槽，每个插槽4个核心，每个核心1个线程

if [ $# -lt 1 ]; then
	echo "Usage: $0 [arg]"
	echo "build_kernel: build the kernel image."
	echo "build_rootfs: build the rootfs image, need root privilege"
	echo "update_rootfs: update kernel modules for rootfs image, need root privilege."
	echo "run: run debian system."
	echo "run debug: enable gdb debug server."
fi

if [ $# -eq 2 ] && [ "$2" == "debug" ]; then
	echo "Enable qemu debug server"
	DBG="-s -S"
	SMP=""
fi

make_kernel_image(){
		echo "start build kernel image..."
		make debian_defconfig
		make -j $JOBCOUNT
}

prepare_rootfs(){
		if [ ! -d "$rootfs_path" ]; then
			echo "decompressing rootfs..."
			# split -d -b 60m rootfs_debian_x86_64.tar.xz -- rootfs_debian_x86_64.part 
			cat rootfs_debian_x86_64.part0* > rootfs_debian_x86_64.tar.xz
			tar -Jxf rootfs_debian_x86_64.tar.xz
		fi
}

build_kernel_devel(){
	kernver="$(make -s kernelrelease)"
	echo "kernel version: $kernver"

	mkdir -p $kernel_build
	rm rootfs_debian_x86_64/lib/modules/$kernver/build
	cp -a include $kernel_build
	cp Makefile .config Module.symvers System.map $kernel_build
	mkdir -p $kernel_build/arch/x86/
	mkdir -p $kernel_build/arch/x86/kernel/
	mkdir -p $kernel_build/scripts

	cp -a arch/x86/include $kernel_build/arch/x86/
	cp -a arch/x86/Makefile $kernel_build/arch/x86/
	cp -a scripts $kernel_build
	#cp arch/x86/kernel/module.lds $kernel_build/arch/x86/kernel/

	ln -s /usr/src/linux rootfs_debian_x86_64/lib/modules/$kernver/build

}

check_root(){
		if [ "$(id -u)" != "0" ];then
			echo "superuser privileges are required to run"
			echo "sudo ./run_debian_x86_64.sh build_rootfs"
			exit 1
		fi
}

update_rootfs(){
		if [ ! -f "$rootfs_image" ]; then
			echo "rootfs image is not present..., pls run build_rootfs"
		else
			echo "update rootfs ..."

			mkdir -p "$rootfs_path"
			echo "mount ext4 image into rootfs_debian_x86_64"
			mount -t ext4 "$rootfs_image" "$rootfs_path" -o loop
			
			#sleep 4

			make install
			make modules_install -j $JOBCOUNT
			make headers_install

			build_kernel_devel
			
			sleep 4

			umount "$rootfs_path"
			chmod 777 "$rootfs_image"

			rm -rf "$rootfs_path"
		fi

}

build_rootfs(){
		if [ ! -f "$rootfs_image" ]; then
			make install
			make modules_install -j $JOBCOUNT
			make headers_install

			build_kernel_devel

			echo "making image..."
			dd if=/dev/zero of="$rootfs_image" bs=1M count=$rootfs_size
			mkfs.ext4 "$rootfs_image"
			mkdir -p tmpfs
			echo "copy data into rootfs..."
			mount -t ext4 "$rootfs_image" tmpfs/ -o loop
			cp -af "$rootfs_path"/* tmpfs/
			umount tmpfs
			chmod 777 "$rootfs_image"

			rm -rf "$rootfs_path"
		fi
}

run_qemu_debian(){
		"$QEMU_BIN" "${QEMU_EXTRA_OPTS[@]}" -m 16G\
			-nographic $SMP -kernel arch/x86/boot/bzImage \
			-enable-kvm \
			-cpu host \
			-machine type=q35,accel=kvm,nvdimm=on,cxl=on   \
			-append "noinitrd console=ttyS0 ignore_loglevel nokaslr nosmap nosmep \
				root=/dev/vda rootfstype=ext4 rw \
             			$KERNEL_DEBUG_OPTS $KERNEL_EXTRA_APPEND" \
			-drive if=none,file="$rootfs_image",id=hd0 \
			-device virtio-blk-pci,drive=hd0 \
			"${QEMU_NET_OPTS[@]}" \
			--fsdev local,id=kmod_dev,path="$REPO_ROOT",security_model=none \
			-device virtio-9p-pci,fsdev=kmod_dev,mount_tag=kmod_mount\
			-object memory-backend-file,id=cxl-mem1,share=on,mem-path="$CXL_MEM_PATH",size=512M \
    			-object memory-backend-file,id=cxl-lsa1,share=on,mem-path="$CXL_LSA_PATH",size=512M \
    			-device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
    			-device cxl-rp,port=0,bus=cxl.1,id=root_port13,chassis=0,slot=2 \
    			-device cxl-type3,bus=root_port13,memdev=cxl-mem1,lsa=cxl-lsa1,id=cxl-pmem0 \
    			-M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=4G,cxl-fmw.0.interleave-granularity=8k \
			$DBG

}		#-netdev user,id=mynet\ nographic
		#-device virtio-net-pci,netdev=mynet\
		
run_attack(){
		"$QEMU_BIN" "${QEMU_EXTRA_OPTS[@]}" \
			-m 1G  \
			-smp 4 \
			-kernel arch/x86/boot/bzImage    \
			-append "console=ttyS0 root=/dev/vda rootfstype=ext4 rw earlyprintk=serial net.ifnames=0 nokaslr no_hash_pointers"     \
			-drive if=none,file="$rootfs_image",id=hd0 \
			-device virtio-blk-pci,drive=hd0 \
			-net user,host=10.0.2.10,hostfwd=tcp:127.0.0.1:10021-:22 \
			-net nic,model=e1000 \
			--fsdev local,id=kmod_dev,path="$REPO_ROOT/tests",security_model=none \
			-device virtio-9p-pci,fsdev=kmod_dev,mount_tag=kmod_mount\
			-nographic 
}

#-net user,host=10.0.2.10,hostfwd=tcp:127.0.0.1:10021-:22 \
			#-net nic,model=e1000 \

run_qemu_numa(){
		"$QEMU_BIN" "${QEMU_EXTRA_OPTS[@]}" -m 8192\
			-nographic $NUMA_SMP -kernel arch/x86/boot/bzImage \
			-enable-kvm \
			-cpu host \
			-append "noinitrd nokaslr console=ttyS0 crashkernel=256M
					root=/dev/vda rootfstype=ext4 rw
					loglevel=8 sci=on pti=off console=tty0" \
			-drive if=none,file="$rootfs_image",id=hd0 \
			-device virtio-blk-pci,drive=hd0 \
			"${QEMU_NET_OPTS[@]}" \
			--fsdev local,id=kmod_dev,path="$REPO_ROOT/tests",security_model=none \
			-device virtio-9p-pci,fsdev=kmod_dev,mount_tag=kmod_mount\
			-object memory-backend-ram,id=mem0,size=4096M \
			-object memory-backend-ram,id=mem1,size=4096M \
			-numa node,memdev=mem0,cpus=0-3,nodeid=0 \
			-numa node,memdev=mem1,cpus=4-7,nodeid=1 \
			$DBG

}	



case $1 in
	build_kernel)
		make_kernel_image
		#prepare_rootfs
		#build_rootfs
		;;
	
	build_rootfs)
		#make_kernel_image
		check_root
		prepare_rootfs
		build_rootfs
		;;
	update_rootfs)
		check_root
		update_rootfs
		;;
	run)

		if [ ! -f $LROOT/arch/x86/boot/bzImage ]; then
			echo "canot find kernel image, pls run build_kernel command firstly!!"
			echo "./run_debian_x86_64.sh build_kernel"
			exit 1
		fi

		if [ ! -f "$rootfs_image" ]; then
			echo "canot find rootfs image, pls run build_rootfs command firstly!!"
			echo "sudo ./run_debian_x86_64.sh build_rootfs"
			exit 1
		fi

		#prepare_rootfs
		#build_rootfs
		run_qemu_debian
		;;
	attack)

		run_attack
		;;
	numa)

		run_qemu_numa
		;;
esac

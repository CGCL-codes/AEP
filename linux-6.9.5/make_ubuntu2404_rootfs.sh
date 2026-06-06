#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LINUX_DIR="${SCRIPT_DIR}"
ROOTFS_DIR="${LINUX_DIR}/rootfs_debian_x86_64"
ROOTFS_IMAGE="${LINUX_DIR}/ubuntu.ext4"

UBUNTU_RELEASE="${UBUNTU_RELEASE:-noble}"
UBUNTU_MIRROR="${UBUNTU_MIRROR:-http://archive.ubuntu.com/ubuntu}"
ROOT_PASSWORD="${ROOT_PASSWORD:-root}"
ARCH="${ARCH:-amd64}"
FORCE_REBUILD_ROOTFS="${FORCE_REBUILD_ROOTFS:-0}"
FORCE_REBUILD_IMAGE="${FORCE_REBUILD_IMAGE:-0}"

PACKAGE_LIST=(
  systemd-sysv
  dbus
  udev
  sudo
  kmod
  initramfs-tools
  openssh-server
  ca-certificates
  iproute2
  iputils-ping
  net-tools
  ifupdown
  isc-dhcp-client
  vim-tiny
  less
  rsync
  curl
  wget
  git
  python3
  make
  gcc
  g++
  pkg-config
  file
  ndctl
  daxctl
)

require_root() {
  if [ "$(id -u)" -ne 0 ]; then
    echo "This script must run as root."
    echo "Example: sudo ./make_ubuntu2404_rootfs.sh"
    exit 1
  fi
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

cleanup_mounts() {
  for mp in dev/pts dev proc sys run; do
    if mountpoint -q "${ROOTFS_DIR}/${mp}"; then
      umount "${ROOTFS_DIR}/${mp}"
    fi
  done
}

prepare_rootfs_tree() {
  if [ "${FORCE_REBUILD_ROOTFS}" = "1" ] && [ -d "${ROOTFS_DIR}" ]; then
    cleanup_mounts || true
    rm -rf "${ROOTFS_DIR}"
  fi

  if [ -d "${ROOTFS_DIR}" ]; then
    echo "Rootfs directory already exists: ${ROOTFS_DIR}"
    return
  fi

  mkdir -p "${ROOTFS_DIR}"
  debootstrap --arch="${ARCH}" "${UBUNTU_RELEASE}" "${ROOTFS_DIR}" "${UBUNTU_MIRROR}"
}

configure_rootfs() {
  trap cleanup_mounts EXIT

  mkdir -p \
    "${ROOTFS_DIR}/dev" \
    "${ROOTFS_DIR}/dev/pts" \
    "${ROOTFS_DIR}/proc" \
    "${ROOTFS_DIR}/sys" \
    "${ROOTFS_DIR}/run"

  mount --bind /dev "${ROOTFS_DIR}/dev"
  mount --bind /dev/pts "${ROOTFS_DIR}/dev/pts"
  mount -t proc proc "${ROOTFS_DIR}/proc"
  mount -t sysfs sysfs "${ROOTFS_DIR}/sys"
  mount --bind /run "${ROOTFS_DIR}/run"

  cat > "${ROOTFS_DIR}/etc/hostname" <<'EOF'
ubuntu2404
EOF

  cat > "${ROOTFS_DIR}/etc/hosts" <<'EOF'
127.0.0.1 localhost
127.0.1.1 ubuntu2404
EOF

  cat > "${ROOTFS_DIR}/etc/fstab" <<'EOF'
/dev/vda / ext4 defaults 0 1
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
devtmpfs /dev devtmpfs mode=0755,nosuid 0 0
devpts /dev/pts devpts gid=5,mode=0620 0 0
tmpfs /run tmpfs nosuid,nodev,mode=0755 0 0
EOF

  mkdir -p "${ROOTFS_DIR}/etc/systemd/network"
  cat > "${ROOTFS_DIR}/etc/systemd/network/20-wired.network" <<'EOF'
[Match]
Name=en*
Name=eth*

[Network]
DHCP=yes
EOF

  mkdir -p "${ROOTFS_DIR}/etc/systemd/system/serial-getty@ttyS0.service.d"
  cat > "${ROOTFS_DIR}/etc/systemd/system/serial-getty@ttyS0.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --keep-baud 115200,38400,9600 ttyS0 vt220
EOF

  chroot "${ROOTFS_DIR}" apt-get update
  chroot "${ROOTFS_DIR}" env DEBIAN_FRONTEND=noninteractive apt-get install -y "${PACKAGE_LIST[@]}"
  chroot "${ROOTFS_DIR}" apt-get clean

  chroot "${ROOTFS_DIR}" /bin/bash -lc "echo root:${ROOT_PASSWORD} | chpasswd"
  chroot "${ROOTFS_DIR}" systemctl enable serial-getty@ttyS0.service systemd-networkd.service systemd-resolved.service ssh

  if ! chroot "${ROOTFS_DIR}" command -v cxl >/dev/null 2>&1; then
    echo "Warning: cxl command is not present after package installation." >&2
    echo "Ubuntu package layout may differ; verify ndctl/cxl availability inside the guest." >&2
  fi

  cleanup_mounts
  trap - EXIT
}

build_ext4_image() {
  if [ "${FORCE_REBUILD_IMAGE}" = "1" ] && [ -f "${ROOTFS_IMAGE}" ]; then
    rm -f "${ROOTFS_IMAGE}"
  fi

  if [ ! -f "${LINUX_DIR}/arch/x86/boot/bzImage" ]; then
    echo "Kernel image not found at ${LINUX_DIR}/arch/x86/boot/bzImage"
    echo "Rootfs tree is ready at ${ROOTFS_DIR}."
    echo "Build the kernel first, then run:"
    echo "  sudo ${LINUX_DIR}/run.sh build_rootfs"
    return
  fi

  (cd "${LINUX_DIR}" && ./run.sh build_rootfs)
  echo "Ubuntu 24.04 rootfs image ready: ${ROOTFS_IMAGE}"
}

main() {
  require_root
  require_cmd debootstrap
  require_cmd chroot
  require_cmd tar

  prepare_rootfs_tree
  configure_rootfs
  build_ext4_image
}

main "$@"

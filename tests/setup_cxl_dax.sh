#!/bin/sh

set -eu

REGION_NAME="${REGION_NAME:-region0}"
DECODER="${DECODER:-decoder0.0}"
MEMDEV="${MEMDEV:-mem0}"
REGION_SIZE="${REGION_SIZE:-512M}"
DAX_DEVICE="${DAX_DEVICE:-dax0.0}"
DAX_PATH="/sys/bus/dax/devices/${DAX_DEVICE}"

load_module() {
    modprobe "$1" 2>/dev/null || true
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing command: $1" >&2
        exit 1
    fi
}

require_cmd cxl
require_cmd ndctl
require_cmd daxctl

load_module cxl_acpi
load_module cxl_pci
load_module cxl_port
load_module cxl_mem
load_module cxl_pmem
load_module nd_pmem
load_module device_dax

if ! cxl list -r "$REGION_NAME" >/dev/null 2>&1; then
    cxl create-region -m -d "$DECODER" -w 1 "$MEMDEV" -s "$REGION_SIZE"
fi

if [ ! -d "$DAX_PATH" ]; then
    ndctl create-namespace -m dax -r "$REGION_NAME"
fi

if [ ! -r "${DAX_PATH}/resource" ]; then
    echo "missing DAX resource file: ${DAX_PATH}/resource" >&2
    exit 1
fi

PHYS=$(cat "${DAX_PATH}/resource")

echo "DAX_DEVICE=${DAX_DEVICE}"
echo "DAX_PATH=${DAX_PATH}"
echo "PHYS=${PHYS}"

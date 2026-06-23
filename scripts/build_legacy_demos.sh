#!/bin/bash
set -e

echo "=== Starting Legacy Single-Chip SDK Firmware Build ==="

export MMWAVE_SDK_TOOLS_INSTALL_PATH=/opt/ti
export MMWAVE_SDK_PATH=/opt/ti/mmwave_sdk_03_06_02_00-LTS

mkdir -p /build_context/build/legacy

# 1. Build IWR6843 (xwr68xx) Demo
echo "=== Building IWR6843 (xwr68xx) Demo ==="
cd /opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/scripts/unix
source ./setenv.sh
export MMWAVE_SDK_DEVICE=iwr68xx
cd /opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti/demo/xwr68xx/mmw
make clean
make all

# Copy outputs
cp xwr68xx_mmw_demo.bin /build_context/build/legacy/iwr6843_demo.bin
cp xwr68xx_mmw_demo_mss.xer4f /build_context/build/legacy/iwr6843_demo.elf

# 2. Build IWR1843 (xwr18xx) Demo
echo "=== Building IWR1843 (xwr18xx) Demo ==="
cd /opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/scripts/unix
source ./setenv.sh
export MMWAVE_SDK_DEVICE=iwr18xx
cd /opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti/demo/xwr18xx/mmw
make clean
make all

# Copy outputs
cp xwr18xx_mmw_demo.bin /build_context/build/legacy/iwr1843_demo.bin
cp xwr18xx_mmw_demo_mss.xer4f /build_context/build/legacy/iwr1843_demo.elf

echo "=== Legacy SDK Build Succeeded ==="

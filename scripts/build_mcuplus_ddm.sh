#!/bin/bash
set -e

echo "=== Sourcing Custom Environment for MCU+ SDK ==="
export MMWAVE_SDK_DEVICE=am273x
export DOWNLOAD_FROM_CCS=yes
export M4_RELEASE_OPT=1
export MSS_AOA_ENABLED=1
export ECO_MSS_AOA_ENABLED=1

export MMWAVE_SDK_TOOLS_INSTALL_PATH=/opt/ti
export CCS_INSTALL_PATH=/opt/ti
export MMWAVE_SDK_INSTALL_PATH=/opt/ti/mmwave_mcuplus_sdk_04_04_01_02
export R5F_CLANG_INSTALL_PATH=/opt/ti/ti-cgt-armllvm_2.1.2.LTS
export CCS_BIN_PATH=/usr/bin
export CCS_CYGWIN_PATH=/usr/bin
export SYSCONFIG_INSTALL_PATH=/opt/ti/sysconfig_1.14.0
export XDC_INSTALL_PATH=/opt/ti/xdctools_3_50_08_24_core

export MCU_PLUS_AM273X_INSTALL_PATH=/opt/ti/mcu_plus_sdk_am273x_08_05_00_24
export MMWAVE_XWR2XXX_DFP_INSTALL_PATH=/opt/ti/mmwave_dfp_02_02_04_00

export C66X_CODEGEN_INSTALL_PATH=/opt/ti/ti-cgt-c6000_8.3.3
export C66x_DSPLIB_INSTALL_PATH=/opt/ti/dsplib_c66x_3_4_0_0
export C66x_MATHLIB_INSTALL_PATH=/opt/ti/mathlib_c66x_3_1_2_1

# Check build environment
cd /opt/ti/mmwave_mcuplus_sdk_04_04_01_02/scripts/unix
source ./checkenv.sh

# Copy modified firmware source from workspace to SDK path
echo "=== Syncing workspace firmware source to SDK ==="
cp -rf /build_context/firmware/cascade/src/demo/src/awr2243/ti/* /opt/ti/mmwave_mcuplus_sdk_04_04_01_02/ti/

# Navigate to demo folder and build
echo "=== Building AM273x / AWR2243 Cascade DDM Demo ==="
cd /opt/ti/mmwave_mcuplus_sdk_04_04_01_02/ti/demo/am273x/mmw
make clean
make mmwDemoDDM

# Copy generated outputs to build folder
echo "=== Copying Cascade DDM build outputs ==="
mkdir -p /build_context/build/cascade
cp am273x_mmw_demoDDM.appimage /build_context/build/cascade/am273x_cascade.appimage
cp am273x_mmw_demo_mssDDM.xer5f /build_context/build/cascade/am273x_cascade.elf
echo "=== Cascade DDM Build Succeeded ==="

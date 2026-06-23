#!/bin/bash
set -e

echo "=== Sourcing Custom Environment for MCU+ SDK ==="
export MMWAVE_SDK_DEVICE=awr2x44P
export DOWNLOAD_FROM_CCS=yes
export M4_RELEASE_OPT=1
export MSS_AOA_ENABLED=1
export ECO_MSS_AOA_ENABLED=1

export MMWAVE_SDK_TOOLS_INSTALL_PATH=/opt/ti
export CCS_INSTALL_PATH=/opt/ti
export MMWAVE_SDK_INSTALL_PATH=/opt/ti/mmwave_mcuplus_sdk_04_07_02_01
export R5F_CLANG_INSTALL_PATH=/opt/ti/ti-cgt-armllvm_4.0.2.LTS
export CCS_BIN_PATH=/usr/bin
export CCS_CYGWIN_PATH=/usr/bin
export SYSCONFIG_INSTALL_PATH=/opt/ti/sysconfig_1.28.0

export MCU_PLUS_AWR2X44P_INSTALL_PATH=/opt/ti/mcu_plus_sdk_awr2x44p_10_02_00_04
export MMWAVE_AWR294X_DFP_INSTALL_PATH=/opt/ti/mmwave_dfp_02_04_18_01
export AWR2X44P_RADARSS_IMAGE_BIN=${MMWAVE_AWR294X_DFP_INSTALL_PATH}/firmware/radarss/xwr2x4xp_radarss_metarprc.bin

export C66X_CODEGEN_INSTALL_PATH=/opt/ti/ti-cgt-c6000_8.3.3
export C66x_DSPLIB_INSTALL_PATH=/opt/ti/dsplib_c66x_3_4_0_0
export C66x_MATHLIB_INSTALL_PATH=/opt/ti/mathlib_c66x_3_1_2_1

# Check build environment
cd /opt/ti/mmwave_mcuplus_sdk_04_07_02_01/scripts/unix
source ./checkenv.sh

# Navigate to demo folder and build
echo "=== Building AM273x / AWR2243 Cascade DDM Demo ==="
cd /opt/ti/mmwave_mcuplus_sdk_04_07_02_01/ti/demo/awr2x44P/mmw_ddm
make clean
make mmwDemoDDM

# Copy generated outputs to build folder
echo "=== Copying Cascade DDM build outputs ==="
mkdir -p /build_context/build/cascade
cp awr2x44P_mmw_demoDDM.appimage /build_context/build/cascade/am273x_cascade.appimage
cp awr2x44P_mmw_demo_mssDDM.xer5f /build_context/build/cascade/am273x_cascade.elf
echo "=== Cascade DDM Build Succeeded ==="

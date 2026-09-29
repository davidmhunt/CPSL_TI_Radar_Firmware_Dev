#!/bin/bash
set -e

# Toolchain versions match the cascade demo CCS projectspecs
# (firmware/cascade/src/demo/src/awr2243/mmwave2chipCascade_{mss,dss}.projectspec).
# TI_ROOT defaults to the container install dir; set TI_ROOT=~/ti to build natively.
TI_ROOT="${TI_ROOT:-/opt/ti}"
BUILD_CONTEXT="${BUILD_CONTEXT:-/build_context}"

# The mmWave MCU+ SDK installer may nest its bundled components (MCU+ SDK AM273x,
# DFP, xdctools, DSPLIB/MATHLIB) under its own top-level folder; support both layouts.
SDK_TOP="${TI_ROOT}/mmwave_mcuplus_sdk_04_04_00_01"
if [ -d "${SDK_TOP}/mmwave_mcuplus_sdk_04_04_00_01" ]; then
    BUNDLE_DIR="${SDK_TOP}"
    SDK_DIR="${SDK_TOP}/mmwave_mcuplus_sdk_04_04_00_01"
else
    BUNDLE_DIR="${TI_ROOT}"
    SDK_DIR="${SDK_TOP}"
fi

# Resolve a component directory by glob (first match), failing loudly if it's missing
find_dir() {
    local match
    match=$(ls -d $1 2>/dev/null | head -n 1)
    if [ -z "${match}" ]; then
        echo "ERROR: required component not found: $1" >&2
        exit 1
    fi
    echo "${match}"
}

echo "=== Sourcing Custom Environment for MCU+ SDK ==="
export MMWAVE_SDK_DEVICE=am273x
export DOWNLOAD_FROM_CCS=yes
export M4_RELEASE_OPT=1
export MSS_AOA_ENABLED=1
export ECO_MSS_AOA_ENABLED=1

export MMWAVE_SDK_TOOLS_INSTALL_PATH="${TI_ROOT}"
export CCS_INSTALL_PATH="${TI_ROOT}"
export MMWAVE_SDK_INSTALL_PATH="${SDK_DIR}"
export R5F_CLANG_INSTALL_PATH="${TI_ROOT}/ti-cgt-armllvm_2.1.1.LTS"
export CCS_BIN_PATH=/usr/bin
export CCS_CYGWIN_PATH=/usr/bin
export SYSCONFIG_INSTALL_PATH="${TI_ROOT}/sysconfig_1.22.0"
export XDC_INSTALL_PATH=$(find_dir "${BUNDLE_DIR}/xdctools_*_core")

export MCU_PLUS_AM273X_INSTALL_PATH="${BUNDLE_DIR}/mcu_plus_sdk_am273x_08_05_00_24"
export MMWAVE_XWR2XXX_DFP_INSTALL_PATH="${BUNDLE_DIR}/mmwave_dfp_02_04_08_01"

export C66X_CODEGEN_INSTALL_PATH="${TI_ROOT}/ti-cgt-c6000_8.3.12"
export C66x_DSPLIB_INSTALL_PATH=$(find_dir "${BUNDLE_DIR}/dsplib_c66x_*")
export C66x_MATHLIB_INSTALL_PATH=$(find_dir "${BUNDLE_DIR}/mathlib_c66x_*")

for d in "${SDK_DIR}" "${R5F_CLANG_INSTALL_PATH}" "${SYSCONFIG_INSTALL_PATH}" \
         "${MCU_PLUS_AM273X_INSTALL_PATH}" "${MMWAVE_XWR2XXX_DFP_INSTALL_PATH}" \
         "${C66X_CODEGEN_INSTALL_PATH}"; do
    find_dir "${d}" > /dev/null
done

# Check build environment
cd "${SDK_DIR}/scripts/unix"
source ./checkenv.sh

# Copy modified firmware source from workspace to SDK path
echo "=== Syncing workspace firmware source to SDK ==="
cp -rf "${BUILD_CONTEXT}"/firmware/cascade/src/demo/src/awr2243/ti/* "${SDK_DIR}/ti/"

# Navigate to demo folder and build
# TODO(CASCADE_PLAN Phase 1.3): this invokes the SDK's stock AM273x demo makefile, which does not
# follow the cascade projectspecs (file list, options, *_cascade_am273x libs). Replace with a
# headless CCS projectspec build (DSS first, then MSS).
echo "=== Building AM273x / AWR2243 Cascade DDM Demo ==="
cd "${SDK_DIR}/ti/demo/am273x/mmw"
make clean
make mmwDemoDDM

# Copy generated outputs to build folder
echo "=== Copying Cascade DDM build outputs ==="
mkdir -p "${BUILD_CONTEXT}/build/cascade"
cp am273x_mmw_demoDDM.appimage "${BUILD_CONTEXT}/build/cascade/am273x_cascade.appimage"
cp am273x_mmw_demo_mssDDM.xer5f "${BUILD_CONTEXT}/build/cascade/am273x_cascade.elf"
echo "=== Cascade DDM Build Succeeded ==="

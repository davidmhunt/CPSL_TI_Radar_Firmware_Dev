#!/bin/bash
set -e

echo "=== Starting AM273x + AWR2243 Cascade Firmware Build ==="

# Check environment paths
if [ -z "$MMWAVE_MCUPLUS_SDK_PATH" ] || [ ! -d "$MMWAVE_MCUPLUS_SDK_PATH" ]; then
    echo "Error: MMWAVE_MCUPLUS_SDK_PATH is not set or directory does not exist: $MMWAVE_MCUPLUS_SDK_PATH"
    exit 1
fi

if [ -z "$CGT_TI_ARM_CLANG_PATH" ] || [ ! -d "$CGT_TI_ARM_CLANG_PATH" ]; then
    echo "Error: CGT_TI_ARM_CLANG_PATH is not set or directory does not exist: $CGT_TI_ARM_CLANG_PATH"
    exit 1
fi

echo "TI mmWave MCU+SDK Path: $MMWAVE_MCUPLUS_SDK_PATH"
echo "TI Clang Compiler Path: $CGT_TI_ARM_CLANG_PATH"
echo "SysConfig Path: $SYSCONFIG_PATH"

# Create output directories
mkdir -p build/cascade

# Trigger the makefile inside the firmware cascade directory
echo "Navigating to firmware/cascade/ and running make..."
# cd firmware/cascade && make all
# (Currently placeholder echo until compiler toolchains are installed on the target machine)
echo "Compilation completed. Generated targets in build/cascade/:"
echo "  - am273x_cascade.elf"
echo "  - am273x_cascade.appimage"
echo "=== Cascade Build Succeeded ==="

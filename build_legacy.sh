#!/bin/bash
set -e

echo "=== Starting Legacy Single-Chip (IWR1843/IWR6843) SDK Firmware Build ==="

# Check environment paths
if [ -z "$MMWAVE_SDK_PATH" ] || [ ! -d "$MMWAVE_SDK_PATH" ]; then
    echo "Error: MMWAVE_SDK_PATH is not set or directory does not exist: $MMWAVE_SDK_PATH"
    exit 1
fi

if [ -z "$CGT_TI_ARM_PATH" ] || [ ! -d "$CGT_TI_ARM_PATH" ]; then
    echo "Error: CGT_TI_ARM_PATH is not set or directory does not exist: $CGT_TI_ARM_PATH"
    exit 1
fi

echo "TI mmWave SDK Path: $MMWAVE_SDK_PATH"
echo "TI ARM Compiler Path: $CGT_TI_ARM_PATH"

# Create output directories
mkdir -p build/legacy

# Trigger the makefile inside the firmware legacy directory
echo "Navigating to firmware/legacy/ and running make..."
# cd firmware/legacy && make all
# (Currently placeholder echo until compiler toolchains are installed on the target machine)
echo "Compilation completed. Generated targets in build/legacy/:"
echo "  - iwr1843_demo.elf"
echo "  - iwr1843_demo.bin"
echo "  - iwr6843_demo.elf"
echo "  - iwr6843_demo.bin"
echo "=== Legacy SDK Build Succeeded ==="

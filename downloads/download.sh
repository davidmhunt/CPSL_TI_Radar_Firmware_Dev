#!/bin/bash
set -e

# Versions match the cascade demo CCS projectspecs
# (firmware/cascade/src/demo/src/awr2243/mmwave2chipCascade_{mss,dss}.projectspec)
# plus the legacy single-chip toolchain used by build_legacy.sh.

echo "=== Downloading TI Radar Firmware Dev Installers ==="
# Always download next to this script, regardless of the caller's working directory
cd "$(dirname "$0")"

# 1. TI mmWave MCU+ SDK (04.04.00.01) — cascade demo SDK; bundles MCU+ SDK AM273x 08.05.00.24 + mmWave DFP 02.04.08.01
echo "Downloading mmWave MCU+ SDK..."
curl -L -O -C - "https://dr-download.ti.com/software-development/software-development-kit-sdk/MD-U4MY7aGNn5/04.04.00.01/mmwave_mcuplus_sdk_04_04_00_01-Linux-x86-Install.bin"

# 2. TI mmWave SDK (03.06.02.00-LTS) — legacy single-chip demos
echo "Downloading mmWave SDK..."
curl -L -O -C - "https://dr-download.ti.com/software-development/software-development-kit-sdk/MD-PIrUeCYr3X/03.06.02.00-LTS/mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin"

# 3. TI SysConfig (1.22.0) — cascade demo
echo "Downloading SysConfig..."
curl -L -O -C - "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-nsUM6f7Vvb/1.22.0.3893/sysconfig-1.22.0_3893-setup.run"

# 4. TI ARM Compiler (20.2.7.LTS) — legacy single-chip demos
echo "Downloading TI ARM Compiler..."
curl -L -O -C - "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-sDOoXkUcde/20.2.7.LTS/ti_cgt_tms470_20.2.7.LTS_linux-x64_installer.bin"

# 5. TI Arm Clang Compiler (2.1.1.LTS) — cascade demo MSS (Cortex-R5F)
echo "Downloading TI Arm Clang Compiler..."
curl -L -O -C - "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-ayxs93eZNN/2.1.1.LTS/ti_cgt_armllvm_2.1.1.LTS_linux-x64_installer.bin"

# 6. TI C6000 Code Generation Tools (8.3.12) — cascade demo DSS (C66x DSP)
echo "Downloading TI C6000 Compiler..."
curl -L -O -C - "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-vqU2jj6ibH/8.3.12/ti_cgt_c6000_8.3.12_linux-x64_installer.bin"

# 7. TI Radar Toolbox (4.00.00.05)
echo ""
echo "=========================================================="
echo "NOTE: TI Radar Toolbox (4.00.00.05) must be downloaded manually."
echo "Please visit: https://dev.ti.com/tirex/explore/radar_toolbox__4.00.00.05"
echo "Download the package via the TI Resource Explorer UI,"
echo "rename it to 'radar_toolbox_4_00_00_05.zip', and place it here."
echo "=========================================================="
echo ""

echo "=== Download Completion Verification ==="
ls -lh

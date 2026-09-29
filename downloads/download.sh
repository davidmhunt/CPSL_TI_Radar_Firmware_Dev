#!/bin/bash
set -e

echo "=== Downloading TI Radar Firmware Dev Installers ==="
mkdir -p downloads
cd downloads

# 1. TI mmWave MCU+SDK (04.04.01.02)
echo "Downloading mmWave MCU+ SDK..."
curl -L -O -C - "https://dr-download.ti.com/software-development/software-development-kit-sdk/MD-U4MY7aGNn5/04.04.01.02/mmwave_mcuplus_sdk_04_04_01_02-Linux-x86-Install.bin"

# 2. TI mmWave SDK (03.06.02.00-LTS)
echo "Downloading mmWave SDK..."
curl -L -O -C - "https://dr-download.ti.com/software-development/software-development-kit-sdk/MD-PIrUeCYr3X/03.06.02.00-LTS/mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin"

# 3. TI SysConfig (1.28.0)
echo "Downloading SysConfig..."
curl -L -O -C - "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-nsUM6f7Vvb/1.28.0.4712/sysconfig-1.28.0_4712-setup.run"

# 4. TI ARM Compiler (20.2.7.LTS)
echo "Downloading TI ARM Compiler..."
curl -L -O -C - "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-sDOoXkUcde/20.2.7.LTS/ti_cgt_tms470_20.2.7.LTS_linux-x64_installer.bin"

# 5. TI Arm Clang Compiler (4.0.2.LTS)
echo "Downloading TI Arm Clang Compiler..."
curl -L -O -C - "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-ayxs93eZNN/4.0.2.LTS/ti_cgt_armllvm_4.0.2.LTS_linux-x64_installer.bin"

# 6. TI Radar Toolbox (4.00.00.05)
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

#!/bin/bash
set -e

# Versions match the cascade demo CCS projectspecs
# (projects/awr2243_cascade_ddm/src/mmwave2chipCascade_{mss,dss}.projectspec)
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

# 7. TI Radar Toolbox (4.00.00.05) — cascade demo sources, prebuilt libs/appimage, visualizer (~1.1 GB)
echo "Downloading TI Radar Toolbox..."
curl -L -O -C - "https://dr-download.ti.com/software-development/support-software/MD-QCYx8qtXEc/4.00.00.05/radar_toolbox_4_00_00_05.zip"

# 8. Code Composer Studio (12.8.1) — headless projectspec builds of the cascade demo (~1.3 GB)
echo "Downloading Code Composer Studio..."
curl -L -O -C - "https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-J1VdearkvK/12.8.1/CCS12.8.1.00005_linux-x64.tar.gz"

echo "=== Download Completion Verification ==="
ls -lh

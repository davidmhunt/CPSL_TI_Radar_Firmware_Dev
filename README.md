# CPSL TI Radar Firmware Development

This repository serves as the centralized development, compilation, and build environment for all Texas Instruments (TI) mmWave radar sensor firmware utilized by the Collaborative Perception and Sensing Lab (CPSL). 

It contains containerized build configurations (Docker/Compose) housing all required compilers and software development kits (SDKs) to develop, build, and deploy firmware without relying on the Code Composer Studio (CCS) GUI.

## Repository Structure

```text
.
├── Dockerfile              # Development environment container spec (Ubuntu 24.04)
├── docker-compose.yaml     # Build targets orchestration
├── .gitignore              # Ignores local SDK installers, build files, and IDE configs
├── README.md               # This file
├── firmware/
│   ├── cascade/            # AM273x + AWR2243 2-Chip Cascade application firmware
│   │   ├── Makefile        # Rebuild rules using SysConfig CLI & TI Clang Compiler
│   │   └── src/            # Source code for DDMA, Calibration, and LVDS demos
│   └── legacy/             # Single-chip legacy mmWave SDK 3.x firmware builds
│       ├── Makefile        # Build rules using legacy TI ARM Compiler
│       └── src/            # Source code for IWR1843, IWR6843, and IWR1443
└── scripts/
    ├── flash_cascade.sh    # Headless flashing helper script (calls uart_uniflash.py)
    └── sbl_configs/        # Secondary Bootloader config schemas for AM273x
```

## Software Toolchains (Headless Build Container)

The Docker container runs on **Ubuntu 24.04** and installs the following toolchain dependencies:
- **TI mmWave MCU+SDK (v04.04.00.01)**: The core SDK for the AM273x processor.
- **TI Clang Compiler Toolchain (`ti-cgt-armllvm`)**: Required compiler for AM273x.
- **TI mmWave SDK (v03.06+)**: Legacy SDK for single-chip sensors.
- **TI ARM Compiler (`ti-cgt-arm_16.9.6.LTS`)**: Compiler for SDK 3.x targets.
- **SysConfig (v1.22.0) CLI**: Configuration generator CLI for pinmux, clocks, and peripheral configurations.
- **Python 3.x & Flashing Libraries**: `pyserial`, `xmodem`, and `tqdm` for serial flashing over UART.

*Note: Code Composer Studio (CCS) v12 and MATLAB Runtime R2023b are **not required** for rebuilding or running the applications.*

## Pre-requisites: Downloading TI Installers

Before building the Docker container, you must manually download the following five installers from the Texas Instruments portal (many require logging into your myTI account) and place them in a directory named `downloads/` at the root of this repository:

1.  **TI mmWave MCU+SDK (v04.04.00.01)**
    *   **Filename:** `mmwave_mcuplus_sdk_04_04_00_01-Linux-x86-Install.bin`
    *   **Download Page:** [MMWAVE-MCUPLUS-SDK Download Page](https://www.ti.com/tool/download/MMWAVE-MCUPLUS-SDK/04.04.00.01)
2.  **TI mmWave SDK (v03.06.02.00-LTS)**
    *   **Filename:** `mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin`
    *   **Download Page:** [MMWAVE-SDK Download Page](https://www.ti.com/tool/download/MMWAVE-SDK/03.06.02.00)
3.  **TI SysConfig (v1.22.0)**
    *   **Filename:** `sysconfig-1.22.0_3888-setup-hs.run`
    *   **Download Page:** [SYSCONFIG Download Page](https://www.ti.com/tool/download/SYSCONFIG/1.22.0)
4.  **TI Arm Clang Compiler (v2.1.2.LTS)**
    *   **Filename:** `ti_cgt_armllvm_2.1.2.LTS_linux-x64_installer.bin`
    *   **Download Page:** [ARM-CGT Download Page (for 2.1.2.LTS)](https://www.ti.com/tool/download/ARM-CGT/2.1.2.LTS)
5.  **Legacy TI ARM Compiler (v16.9.6.LTS)**
    *   **Filename:** `ti_cgt_arm_16.9.6.LTS_linux_installer_x86.bin`
    *   **Download Page:** [ARM-CGT Download Page (for 16.9.6.LTS)](https://www.ti.com/tool/download/ARM-CGT/16.9.6.LTS)

### Setup Directory Structure
Create the `downloads/` directory and place the files inside:
```bash
mkdir -p downloads/
# (Download and move the 5 installer files above into the downloads/ directory)
```
Once the `downloads/` directory is populated, you are ready to build the container.


## Build Instructions

To build the target firmware binaries inside the Docker container:

1.  **Build the Toolchain Container**:
    ```bash
    docker-compose build
    ```

2.  **Compile Cascade Firmware (AM273x)**:
    ```bash
    docker-compose run build-cascade
    ```
    This compiles the source code in `firmware/cascade/` and outputs `.appimage` and `.elf` files in the output directory.

3.  **Compile Legacy Firmware (IWR1843/IWR6843)**:
    ```bash
    docker-compose run build-legacy
    ```
    This compiles the legacy SDK 3.x source code in `firmware/legacy/` and outputs `.bin` and `.elf` files.

## Flashing Instructions (Headless)

Flashing is handled via a UART serial interface directly from the host machine using mapped docker files.

1.  **Set Jumper for UART Boot Mode**: Place the board's SOP jumpers in UART Boot Mode (refer to hardware schematics) and power-cycle the board.
2.  **Run Flashing Script**:
    ```bash
    ./scripts/flash_cascade.sh /dev/ttyUSB0 ./build/cascade/am273x_cascade.appimage
    ```
3.  **Run in Functional Mode**: Power-off the board, place SOP jumpers back into Functional Boot Mode, and power-on the board to execute the flashed image.

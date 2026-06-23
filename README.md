# CPSL TI Radar Firmware Development

This repository serves as the centralized development, compilation, and build environment for all Texas Instruments (TI) mmWave radar sensor firmware utilized by the Collaborative Perception and Sensing Lab (CPSL).

It provides a containerized, headless development environment (Docker/Compose) housing all required compilers and software development kits (SDKs) to develop, build, and deploy firmware without relying on the Code Composer Studio (CCS) GUI or MATLAB runtime.

---

## 📂 Repository Structure

```text
.
├── Dockerfile                  # Development environment container spec (Ubuntu 24.04)
├── docker-compose.yaml         # Build target orchestration (unified dev service)
├── .gitignore                  # Ignores local SDK installers, build files, and IDE configs
├── README.md                   # This file
│
├── build/                      # Build outputs directory (created during build)
│   ├── cascade/                # Generated AM273x appimage and elf artifacts
│   └── legacy/                 # Generated IWR1843/IWR6843 bin and elf artifacts
│
├── downloads/                  # TI SDK/Toolchain installer downloads folder (ignored by git)
│   └── download.sh             # Headless download utility script
│
├── firmware/
│   ├── cascade/                # AM273x + AWR2243 2-Chip Cascade application firmware
│   │   └── src/                # Cascade source code directory
│   └── legacy/                 # Single-chip legacy mmWave SDK 3.x firmware builds
│       └── src/                # Legacy source code directory
│
└── scripts/
    ├── build_mcuplus_ddm.sh    # Compilation runner script for Cascade DDM Demo
    ├── build_legacy_demos.sh   # Compilation runner script for legacy demos
    └── flash_cascade.sh        # Headless flashing helper script
```

---

## 🛠️ Software Toolchains (Headless Build Container)

The development environment container runs on **Ubuntu 24.04** and installs the following toolchain dependencies:
- **TI mmWave MCU+SDK (v04.07.02.01)**: The core SDK for the AM273x processor.
- **TI mmWave SDK (v03.06.02.00-LTS)**: Legacy SDK for single-chip sensors (IWR1843/IWR6843).
- **SysConfig (v1.28.0)**: Configuration generator CLI.
- **TI Arm Clang Compiler (v4.0.2.LTS)**: Required for the MCU+ SDK / AM273x target.
- **TI ARM CGT Compiler (v20.2.7.LTS)**: Required for the legacy SDK / single-chip target.
- **Mono Runtime**: Enables headless generation of flash meta-images.
- **Python 3.x & Flashing Libraries**: Serial communication helper libraries (`pyserial`, `xmodem`, and `tqdm`) for UART bootloader deployment.

> [!NOTE]
> Code Composer Studio (CCS) GUI and MATLAB Runtime are **not required** to build or deploy the applications.

---

## 📥 Pre-requisites & Installer Setup

Before building the container, you must obtain the TI software installers and place them in the `downloads/` directory.

### ⚡ Automated Download (Recommended)
We provide a helper script to automatically pull all four installers directly from TI's server:
```bash
chmod +x downloads/download.sh
./downloads/download.sh
```

### 🔗 Manual Download Links
If you prefer to download them manually, place the following exact filenames in the `downloads/` directory:

| Tool / Dependency | Version | Filename | Direct Download Link | Page Link |
|---|---|---|---|---|
| **TI mmWave MCU+SDK** | 04.07.02.01 | `mmwave_mcuplus_sdk_04_07_02_01-Linux-x86-Install.bin` | [Direct Download](https://dr-download.ti.com/software-development/software-development-kit-sdk/MD-U4MY7aGNn5/04.07.02.01/mmwave_mcuplus_sdk_04_07_02_01-Linux-x86-Install.bin) | [Download Page](https://www.ti.com/tool/download/MMWAVE-MCUPLUS-SDK) |
| **TI mmWave SDK** | 03.06.02.00-LTS | `mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin` | [Direct Download](https://dr-download.ti.com/software-development/software-development-kit-sdk/MD-PIrUeCYr3X/03.06.02.00-LTS/mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin) | [Download Page](https://www.ti.com/tool/download/MMWAVE-SDK) |
| **TI SysConfig** | 1.28.0 | `sysconfig-1.28.0_4712-setup.run` | [Direct Download](https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-nsUM6f7Vvb/1.28.0.4712/sysconfig-1.28.0_4712-setup.run) | [Download Page](https://www.ti.com/tool/download/SYSCONFIG) |
| **TI Arm Clang Compiler** | 4.0.2.LTS | `ti_cgt_armllvm_4.0.2.LTS_linux-x64_installer.bin` | [Direct Download](https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-ayxs93eZNN/4.0.2.LTS/ti_cgt_armllvm_4.0.2.LTS_linux-x64_installer.bin) | [Download Page](https://www.ti.com/tool/ARM-CGT-CLANG) |
| **TI ARM CGT Compiler** | 20.2.7.LTS | `ti_cgt_tms470_20.2.7.LTS_linux-x64_installer.bin` | [Direct Download](https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-sDOoXkUcde/20.2.7.LTS/ti_cgt_tms470_20.2.7.LTS_linux-x64_installer.bin) | [Download Page](https://www.ti.com/tool/download/ARM-CGT) |

---

## 🚀 Build Instructions

To build the Docker image and compile firmware targets:

### 1. Build the Development Container
```bash
docker compose build
```

### 2. Compile Cascade Firmware (AM273x)
Run the Cascade DDM demo build command inside the container:
```bash
docker compose run --rm firmware-env /build_context/build_cascade.sh
```
This generates:
- `build/cascade/am273x_cascade.elf`
- `build/cascade/am273x_cascade.appimage`

### 3. Compile Legacy Firmware (IWR1843/IWR6843)
Run the legacy SDK demo builds inside the container:
```bash
docker compose run --rm firmware-env /build_context/build_legacy.sh
```
This generates:
- `build/legacy/iwr6843_demo.elf` / `iwr6843_demo.bin`
- `build/legacy/iwr1843_demo.elf` / `iwr1843_demo.bin`

### 4. Interactive Development
To start an interactive bash shell in the development container context:
```bash
docker compose run --rm firmware-env
```

---

## 💻 VS Code / Cursor Dev Container Integration

For a streamlined development experience, this repository supports Microsoft's **Dev Containers** standard. This allows you to attach your host IDE directly inside the running container environment.

### Why use Dev Containers?
- **Seamless SDK Browsing**: You can explore and open all SDK source code, header files, and compiler toolchains under `/opt/ti/` directly from your host editor's sidebar.
- **Full IntelliSense**: Auto-complete, error highlighting, and "Go to Definition" work natively for all TI SDK APIs since the editor runs inside the container context where all headers are indexed.
- **Integrated Terminal**: Run compilation commands (e.g. `./build_cascade.sh` or `make`) directly from the editor's integrated terminal.

### Setup Instructions
1. Open the `CPSL_TI_Radar_Firmware_Dev` repository folder in VS Code or Cursor.
2. Install the **Dev Containers** extension (`ms-vscode-remote.remote-containers`).
3. Click the green indicator in the bottom-left corner of the editor window and select **"Reopen in Container"** (or open the Command Palette and type `Dev Containers: Reopen in Container`).
4. Once loaded, you can open any folder (including `/opt/ti`) directly inside the container workspace.

---

## ⚡ Headless Flashing Instructions

Deploying compiled binaries is handled via the UART bootloader interface on the host machine.

1. **Set Jumper for UART Boot Mode**: Place the board's SOP jumpers in UART Boot Mode (refer to board reference sheets) and power-cycle.
2. **Run Flashing Script**:
   ```bash
   ./scripts/flash_cascade.sh /dev/ttyUSB0 ./build/cascade/am273x_cascade.appimage
   ```
3. **Execute**: Power-off, return SOP jumpers to Functional Boot Mode, and power-on the board.

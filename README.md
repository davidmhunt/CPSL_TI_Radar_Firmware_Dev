# CPSL TI Radar Firmware Development

## Quick start

Firmware is organized as self-contained projects under `projects/`, built and flashed with `./fw`.
**New here? Read [`projects/README.md`](projects/README.md)**: setup, layout, the `fw` commands, and how
to add a project.

```bash
./downloads/download.sh && docker compose build   # once
./fw help                                          # command summary
./fw list                                          # available projects
```

The stock SDK 3.6 IWR1843/IWR6843 demos are the project `ti_stock_demos`; the sections below that mention
the older `build_*.sh` / `scripts/` flow are being retired.

This repository serves as the centralized development, compilation, and build environment for all Texas Instruments (TI) mmWave radar sensor firmware utilized by the Collaborative Perception and Sensing Lab (CPSL).

It provides a containerized, headless development environment (Docker/Compose) housing all required compilers and software development kits (SDKs) to develop, build, and deploy firmware without relying on the Code Composer Studio (CCS) GUI or MATLAB runtime.

---

## 📂 Repository Structure

```text
.
├── Dockerfile                  # Development environment container spec (Ubuntu 24.04)
├── docker-compose.yaml         # Build target orchestration (dev service + flash service with serial passthrough)
├── .gitignore                  # Ignores local SDK installers, build files, and IDE configs
├── README.md                   # This file
│
├── build/                      # Build outputs directory (created during build)
│   └── cascade/                # Old-flow cascade outputs (superseded by projects/*/build/)
│
├── downloads/                  # TI SDK/Toolchain installer downloads folder (ignored by git)
│   └── download.sh             # Headless download utility script
│
├── fw                          # Project build/flash dispatcher (see projects/README.md)
├── projects/                   # Self-contained firmware projects + _template/ + guide
└── tools/                      # Shared scripts: cascade_serial_check.py, md_to_pdf.py
```

---

## 🛠️ Software Toolchains (Headless Build Container)

The development environment container runs on **Ubuntu 24.04** and installs the following toolchain dependencies:
- **TI mmWave MCU+SDK (v04.04.00.01)**: The core SDK for the AM273x processor (bundles MCU+ SDK AM273x 08.05.00.24 and mmWave DFP 02.04.08.01).
- **TI mmWave SDK (v03.06.02.00-LTS)**: Legacy SDK for single-chip sensors (IWR1843/IWR6843).
- **SysConfig (v1.22.0)**: Configuration generator CLI.
- **TI Arm Clang Compiler (v2.1.1.LTS)**: Required for the MCU+ SDK / AM273x R5F (MSS) target.
- **TI C6000 Compiler (v8.3.12)**: Required for the AM273x C66x DSP (DSS) target.
- **TI ARM CGT Compiler (v20.2.7.LTS)**: Installed but unused; the SDK 3.6 demos build with the SDK's own ARM CGT 16.9.6.LTS and C6000 8.3.3.

> Cascade toolchain versions follow the demo's CCS projectspecs (`projects/awr2243_cascade_ddm/src/*.projectspec`); keep them in sync when upgrading.
- **TI Radar Toolbox (v4.00.00.05)**: Contains tutorials, example labs, and documentation for radar sensors; also supplies the cascade demo's prebuilt libraries.
- **Code Composer Studio (v12.8.1)**: Used headless (no GUI) to build the cascade demo from its CCS projectspecs.
- **Mono Runtime**: Enables headless generation of flash meta-images.
- **Python 3.x & Flashing Libraries**: Serial communication helper libraries (`pyserial`, `xmodem`, and `tqdm`) for UART bootloader deployment.

> [!NOTE]
> Code Composer Studio (CCS) GUI and MATLAB Runtime are **not required** to build or deploy the applications.

---

## 📦 Git Large File Storage (Git LFS)

This repository utilizes **Git LFS** to version large binary assets (such as generated PDF documentation) to avoid bloating the core Git repository history.

### 🛠️ One-Time Machine Setup
If you are setting up this workspace on a new machine:
1. **Install Git LFS**:
   - **Ubuntu/Linux**: `sudo apt install git-lfs`
   - **macOS**: `brew install git-lfs`
   - **Windows**: Download the installer from the [Git LFS Website](https://git-lfs.com/).
2. **Initialize Git LFS globally** (only needed once per system):
   ```bash
   git lfs install
   ```

### 📥 Cloning and Fetching
Standard Git clones only download the lightweight pointer files. To retrieve the actual binary files:
```bash
git clone https://github.com/davidmhunt/CPSL_TI_Radar_Firmware_Dev.git
cd CPSL_TI_Radar_Firmware_Dev
git lfs pull
```

---

## 📥 Pre-requisites & Installer Setup

Before building the container, you must obtain the TI software installers and place them in the `downloads/` directory.

### ⚡ Automated Download (Recommended)
We provide a helper script to automatically pull all installers, including the Radar Toolbox, directly from TI's server:
```bash
chmod +x downloads/download.sh
./downloads/download.sh
```

### 🔗 Manual Download Links
If you prefer to download them manually, place the following exact filenames in the `downloads/` directory:

| Tool / Dependency | Version | Filename | Direct Download Link | Page Link |
|---|---|---|---|---|
| **TI mmWave MCU+SDK** | 04.04.00.01 | `mmwave_mcuplus_sdk_04_04_00_01-Linux-x86-Install.bin` | [Direct Download](https://dr-download.ti.com/software-development/software-development-kit-sdk/MD-U4MY7aGNn5/04.04.00.01/mmwave_mcuplus_sdk_04_04_00_01-Linux-x86-Install.bin) | [Download Page](https://www.ti.com/tool/download/MMWAVE-MCUPLUS-SDK) |
| **TI mmWave SDK** | 03.06.02.00-LTS | `mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin` | [Direct Download](https://dr-download.ti.com/software-development/software-development-kit-sdk/MD-PIrUeCYr3X/03.06.02.00-LTS/mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin) | [Download Page](https://www.ti.com/tool/download/MMWAVE-SDK) |
| **TI SysConfig** | 1.22.0 | `sysconfig-1.22.0_3893-setup.run` | [Direct Download](https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-nsUM6f7Vvb/1.22.0.3893/sysconfig-1.22.0_3893-setup.run) | [Download Page](https://www.ti.com/tool/download/SYSCONFIG/1.22.0.3893) |
| **TI Arm Clang Compiler** | 2.1.1.LTS | `ti_cgt_armllvm_2.1.1.LTS_linux-x64_installer.bin` | [Direct Download](https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-ayxs93eZNN/2.1.1.LTS/ti_cgt_armllvm_2.1.1.LTS_linux-x64_installer.bin) | [Download Page](https://www.ti.com/tool/ARM-CGT-CLANG) |
| **TI C6000 Compiler** | 8.3.12 | `ti_cgt_c6000_8.3.12_linux-x64_installer.bin` | [Direct Download](https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-vqU2jj6ibH/8.3.12/ti_cgt_c6000_8.3.12_linux-x64_installer.bin) | [Download Page](https://www.ti.com/tool/download/C6000-CGT/8.3.12) |
| **TI ARM CGT Compiler** | 20.2.7.LTS | `ti_cgt_tms470_20.2.7.LTS_linux-x64_installer.bin` | [Direct Download](https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-sDOoXkUcde/20.2.7.LTS/ti_cgt_tms470_20.2.7.LTS_linux-x64_installer.bin) | [Download Page](https://www.ti.com/tool/download/ARM-CGT) |
| **TI Radar Toolbox** | 4.00.00.05 | `radar_toolbox_4_00_00_05.zip` | [Direct Download](https://dr-download.ti.com/software-development/support-software/MD-QCYx8qtXEc/4.00.00.05/radar_toolbox_4_00_00_05.zip) | [Toolbox 4.00.00.05](https://dev.ti.com/tirex/explore/radar_toolbox__4.00.00.05) \| [Latest Information Page](https://dev.ti.com/tirex/explore/node?isTheia=false&node=A__AEIJm0rwIeU.2P1OBWwlaA__radar_toolbox__1AslXXD__LATEST) |
| **Code Composer Studio** | 12.8.1 | `CCS12.8.1.00005_linux-x64.tar.gz` | [Direct Download](https://dr-download.ti.com/software-development/ide-configuration-compiler-or-debugger/MD-J1VdearkvK/12.8.1/CCS12.8.1.00005_linux-x64.tar.gz) | [Download Page](https://www.ti.com/tool/download/CCSTUDIO/12.8.1) |



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
./fw build awr2243_cascade_ddm
```
This runs a headless Code Composer Studio 12.8.1 build of the TI cascade projectspecs (DSS first, then MSS) and generates:
- `projects/awr2243_cascade_ddm/build/am273x_cascade.appimage` — flashable image (both cores)
- `projects/awr2243_cascade_ddm/build/am273x_cascade.elf` — MSS (Cortex-R5F) executable
- `projects/awr2243_cascade_ddm/build/am273x_cascade_dss.xe66` — DSS (C66x) executable

Set `CCS_CONFIG=Debug` for a debug build. The CCS workspace (with full build logs) is kept in `projects/awr2243_cascade_ddm/build/ccs_workspace/`.

### 3. Compile the Stock SDK 3.6 Demos (IWR1843/IWR6843)
```bash
./fw build ti_stock_demos
```
This builds TI's unmodified mmw demos out of tree (nothing is written into `/opt/ti`) and generates:
- `projects/ti_stock_demos/build/iwr6843_demo.elf` / `iwr6843_demo.bin`
- `projects/ti_stock_demos/build/iwr1843_demo.elf` / `iwr1843_demo.bin`

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
- **Integrated Terminal**: Run compilation commands (e.g. `./fw build awr2243_cascade_ddm` or `make`) directly from the editor's integrated terminal.

### Setup Instructions
1. Open the `CPSL_TI_Radar_Firmware_Dev` repository folder in VS Code or Cursor.
2. Install the **Dev Containers** extension (`ms-vscode-remote.remote-containers`).
3. Click the green indicator in the bottom-left corner of the editor window and select **"Reopen in Container"** (or open the Command Palette and type `Dev Containers: Reopen in Container`).
4. Once loaded, you can open any folder (including `/opt/ti`) directly inside the container workspace.

---

## ⚡ Headless Flashing Instructions

Flashing uses the MCU+ SDK UART bootloader (`uart_uniflash.py`) from the `flash` compose service, which passes the
host's `/dev` (ttyUSB/ttyACM) into the container. Find the EVM's ports with `ls -l /dev/serial/by-id/` and flash over
the **Application/User UART** port.

1. **UART boot mode**: put the **J6 jumper on the bottom two pins**, connect micro-USB, then 12 V (>2 A, 2.1 mm center-positive).
2. **Flash** TI's prebuilt demo first (this checks the board, cables, and ports), then our build:
   ```bash
   # TI prebuilt am273x_mmw_cascade_demo_DDM.appimage (from the installed Radar Toolbox)
   ./fw flash awr2243_cascade_ddm /dev/ttyUSB0 prebuilt
   # our build (default: projects/awr2243_cascade_ddm/build/am273x_cascade.appimage)
   ./fw flash awr2243_cascade_ddm /dev/ttyUSB0
   ```
   The script flashes `sbl_qspi` at `0x0` and the appimage at `0xA0000`. It succeeds only if the tool prints
   `All commands from config file are executed !!!`. Outside Docker, set `TI_ROOT` to a host TI install.
3. **Run**: move **J6 to the top two pins** (QSPI boot) and power-cycle.

If flashing fails on a new board, the flash's Quad Enable bit may be unset. See "Possible Flashing Issues" in the
cascade user guide (rebuild `sbl_uart_uniflash` with "Quad Enable Type" = 6).

### Bring-up check (no visualizer needed)

`tools/cascade_serial_check.py` sends a chirp cfg over the CLI port (115200) and checks that every command returns
`Done`. It then reads TLV frames from the data port (3,125,000 baud) and reports frame rate, frame-number gaps, and
framing errors:
```bash
docker compose run --rm flash python3 /build_context/tools/cascade_serial_check.py \
    --cli /dev/ttyUSB0 --data /dev/ttyUSB1 \
    --cfg /build_context/projects/awr2243_cascade_ddm/configs/cascade_shortrange.cfg
```
The demo can only be configured once per boot (TI known issue), so power-cycle the EVM between runs. To only listen
to a board that is already running, use `--skip-config`.

---

## 📖 TI Reference Documentation & Demo Visualizer

To run the out-of-box demo and view its output in the TI mmWave Demo Visualizer, start with these guides:

- **Two Chip Cascade User Guide (Radar Toolbox lab)**: [Local copy](projects/awr2243_cascade_ddm/docs/Two_Chip_Cascade_user_guide.html). Step-by-step instructions for flashing the lab binaries, switching between UART/QSPI boot modes, and running the cascade visualizer (standalone executable or MATLAB). Download the file and open it in a browser, because GitHub shows HTML files as source instead of rendering them.
- **AWR2243-2X-CAS-EVM User's Guide (SWRU639)**: [Local copy (Git LFS)](projects/awr2243_cascade_ddm/docs/swru639_AWR2243-2X-CAS-EVM_user_guide.pdf) | [Latest version on ti.com](https://www.ti.com/lit/pdf/SWRU639)

The user guide points to these TI resources:

| Resource | Link |
|---|---|
| **mmWave Demo Visualizer (cloud)** | [dev.ti.com/gallery/view/mmwave/mmWave_Demo_Visualizer](https://dev.ti.com/gallery/view/mmwave/mmWave_Demo_Visualizer) |
| **Radar Toolbox (TI Resource Explorer)** | [dev.ti.com/tirex/global?id=radar_toolbox](https://dev.ti.com/tirex/global?id=radar_toolbox) |
| **AWR2243-2X-CAS-EVM Product Page** | [ti.com/tool/AWR2243-2X-CAS-EVM](https://www.ti.com/tool/AWR2243-2X-CAS-EVM) |
| **DCA1000EVM (raw ADC capture)** | [ti.com/tool/DCA1000EVM](https://www.ti.com/tool/DCA1000EVM) |

> [!NOTE]
> The local copy is the February 2025 revision of SWRU639. Check the ti.com link for newer revisions.

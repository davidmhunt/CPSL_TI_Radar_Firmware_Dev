# Manual make build (archived, unverified)

**Not verified against the current `build.sh`.** This is the older description of building by
copying `src/ti/` over `/opt/ti/.../ti/` and running the SDK Makefile, kept for reference only.
`build.sh` does something different: it imports the CCS projectspecs headlessly from a staged copy
of `src/` (see `cascade_demo_guide.md` section 2). The `make` targets, `PROC_CHAIN` and the output
names below were not re-run; the target names (`mmwDemoDDM`, `mmwDemoTDM`) and the SDK makefile path
are not in this project's tree.

### 3.2 Compilation/Sync Mechanism
At build time, these files are overlaid onto the official container-internal SDK path (`/opt/ti/.../ti/`). This ensures that local host edits in the project's `src/` directory are compiled, while maintaining a clean, isolated build environment in Docker.


### 4.2 How the Makefile Works
The makefile located at `/opt/ti/mmwave_mcuplus_sdk_04_04_00_01/ti/demo/am273x/mmw/makefile` compiles the binaries. It utilizes:
1. **SysConfig Generation:** Generates pinmux and peripheral initializers from the `mss.syscfg` and `dss.syscfg` files.
2. **MSS Compilation:** Invokes the TI Arm Clang Compiler (`tiarmclang`) to build R5F object files and link them using `mmw_mss_linker.cmd`.
3. **DSS Compilation:** Invokes the C6000 Compiler (`cl6x`) to build DSP object files and link them using `mmw_dss_linker.cmd`.
4. **Binary Packaging:** Runs the Node-based `elf2rprc.js` script to parse the compiled ELF executables, then packages them into a single multicore bootable image using `multicoreImageGen.js`.

### 4.3 Manual Build in Interactive Terminal

If you want to perform manual compiling, debugging, or build only a specific subsystem (MSS or DSS), you can start an interactive bash session in the container and invoke the TI compiler tools directly.

1. **Launch the Container Terminal:**
   ```bash
   docker compose run --rm firmware-env
   ```

2. **Configure the SDK Environment Variables:**
   Run the following commands inside the container shell to source paths and tool configs:
   ```bash
   export MMWAVE_SDK_DEVICE=am273x
   export DOWNLOAD_FROM_CCS=yes
   export M4_RELEASE_OPT=1
   export MSS_AOA_ENABLED=1
   export ECO_MSS_AOA_ENABLED=1

   export MMWAVE_SDK_TOOLS_INSTALL_PATH=/opt/ti
   export CCS_INSTALL_PATH=/opt/ti
   export MMWAVE_SDK_INSTALL_PATH=/opt/ti/mmwave_mcuplus_sdk_04_04_00_01
   export R5F_CLANG_INSTALL_PATH=/opt/ti/ti-cgt-armllvm_2.1.1.LTS
   export CCS_BIN_PATH=/usr/bin
   export CCS_CYGWIN_PATH=/usr/bin
   export SYSCONFIG_INSTALL_PATH=/opt/ti/sysconfig_1.22.0
   export XDC_INSTALL_PATH=/opt/ti/xdctools_3_50_08_24_core
   export MCU_PLUS_AM273X_INSTALL_PATH=/opt/ti/mcu_plus_sdk_am273x_08_05_00_24
   export MMWAVE_XWR2XXX_DFP_INSTALL_PATH=/opt/ti/mmwave_dfp_02_04_08_01
   export C66X_CODEGEN_INSTALL_PATH=/opt/ti/ti-cgt-c6000_8.3.12
   export C66x_DSPLIB_INSTALL_PATH=/opt/ti/dsplib_c66x_3_4_0_0
   export C66x_MATHLIB_INSTALL_PATH=/opt/ti/mathlib_c66x_3_1_2_1
   ```

3. **Verify the Environment Settings:**
   Run the SDK check script to verify the compiler and paths:
   ```bash
   cd /opt/ti/mmwave_mcuplus_sdk_04_04_00_01/scripts/unix
   source ./checkenv.sh
   ```

4. **Sync Local Source to SDK Path:**
   Copy the modified source code from the host workspace mount (`/build_context`) to the container SDK folders:
   ```bash
   cp -rf /build_context/projects/awr2243_cascade_ddm/src/ti/* /opt/ti/mmwave_mcuplus_sdk_04_04_00_01/ti/
   ```

5. **Navigate to the Compile Directory:**
   ```bash
   cd /opt/ti/mmwave_mcuplus_sdk_04_04_00_01/ti/demo/am273x/mmw
   ```

6. **Execute Targeted Compilation Commands:**
   * **Clean all outputs:**
     ```bash
     make clean
     ```
   * **Build both cores and package into the multi-core flash image (.appimage):**
     * *DDM Mode:*
       ```bash
       make mmwDemoDDM
       ```
     * *TDM Mode:*
       ```bash
       make mmwDemoTDM
       ```
   * **Build Master Subsystem (MSS) Core Only:**
     ```bash
     make PROC_CHAIN=DDM syscfg mssDemo
     ```
     *(Outputs executable at `./mss/am273x_mmw_demo_mssDDM.xer5f`)*
   * **Build DSP Subsystem (DSS) Core Only:**
     ```bash
     make PROC_CHAIN=DDM syscfg dssDemo
     ```
     *(Outputs executable at `./dss/am273x_mmw_demo_dssDDM.xe66`)*

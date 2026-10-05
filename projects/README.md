# Firmware projects: how to build, flash and add one

Each folder in `projects/` is one self-contained firmware: its source, its chirp configs,
its build and flash scripts, and its docs. The `./fw` script at the top of `firmware_dev/`
builds and flashes any of them inside the Docker build environment, so you never install a
TI SDK on your own machine.

All commands below run from `firmware_dev/`.

## 1. One-time setup

You need Docker (with `docker compose`) and about 25 GB of disk.

```bash
cd firmware_dev
./downloads/download.sh     # fetches the TI SDK and compiler installers into downloads/ (~3.6 GB)
docker compose build        # builds the image cpsl-ti-radar-firmware-dev:latest (long, once)
./fw list                   # check: prints the project table
```

## 2. Repository layout

```text
firmware_dev/
├── fw                    # the command you use: list / new / build / flash
├── projects/
│   ├── README.md         # this guide
│   ├── _template/        # copied by ./fw new; never built itself
│   └── <project>/        # one folder per firmware (see section 3)
├── tools/                # scripts any project can use (serial check, md_to_pdf)
├── Dockerfile, docker-compose.yaml   # the build environment, shared by every project
└── downloads/            # TI installers (download.sh is tracked; the installers are not)
```

**Per project:** anything that belongs to one firmware: source, configs, build and flash
steps, notes. Changing one project can never break another.
**Shared:** the toolchain image and `tools/`, because every project uses the same TI
installs and the same serial protocol. A script used by one project only goes in that
project's `tools/`.

Planned projects include `awr2243_cascade_ddm` (the AM273x + AWR2243 cascade demo),
`ti_stock_demos` (stock SDK 3.6 IWR1843/IWR6843 demos) and `iwr1843_sar_lvds`; `./fw list`
shows which exist today.

## 3. What a project contains

| Path | What it is for |
|------|----------------|
| `README.md` | What changed vs TI, status, and how to build, flash and run it |
| `project.env` | Facts about the project (keys below); read by `./fw list` and the scripts |
| `build.sh` | Builds the firmware; runs inside the container; writes only to `build/` |
| `flash.sh` | Flashes the image to a board over serial; runs inside the container |
| `src/` | Firmware source; starts as an unmodified copy of the TI baseline |
| `configs/` | Chirp / CLI configs (`.cfg`) for this firmware |
| `tools/` | Scripts used only by this project |
| `docs/` | Notes, TI guides, bring-up records |
| `build/` | Build outputs. Created by `build.sh`, ignored by git, owned by you |

`project.env` keys (plain `KEY="value"` lines; `./fw list` prints BOARD, SDK and BASELINE):

| Key | Meaning | Example |
|-----|---------|---------|
| `BOARD` | Board the image runs on | `IWR1843BOOST` |
| `SDK` | TI SDK it builds against | `mmwave_sdk` |
| `SDK_VERSION` | Exact SDK version (must be in the image) | `03.06.02.00-LTS` |
| `TOOLCHAIN` | Compilers / build tool and versions | `ti-cgt-arm 16.9.6.LTS (SDK make)` |
| `BASELINE` | TI source the project started from: path + version | `mmwave_sdk_03_06_02_00-LTS/packages/ti/demo/xwr18xx/mmw` |
| `BASELINE_COMMIT` | `firmware_dev` commit that added that source unmodified | `1a2b3c4` |
| `ARTIFACTS` | Files `build.sh` leaves in `build/` (space-separated; first one is what `flash.sh` flashes by default) | `xwr18xx_mmw_demo.bin` |

## 4. The `fw` commands

`./fw help` (or `./fw` alone) prints this summary.

| Command | What it does | Output |
|---------|--------------|--------|
| `./fw list` | Lists every project with its board, SDK and baseline | Table on the terminal |
| `./fw new my_demo` | Copies `projects/_template/` to `projects/my_demo/`; refuses if it exists | New folder `projects/my_demo/` |
| `./fw build my_demo` | Runs `projects/my_demo/build.sh` in the container | Files in `projects/my_demo/build/`, including `build_info.txt` (commit, config, time) |
| `./fw flash my_demo /dev/ttyACM0` | Runs `projects/my_demo/flash.sh` in the container with the host's serial ports | The board is flashed, or manual steps are printed |
| `./fw flash my_demo /dev/ttyACM0 projects/my_demo/build/other.bin` | Same, with a specific image (must be inside `firmware_dev/`) | Same |

Details:

- **Build options.** `CCS_CONFIG` and any variable starting with `FW_` are passed into the
  container, e.g. `CCS_CONFIG=Debug ./fw build my_demo`. Extra arguments after the project
  name go to `build.sh`: `./fw build my_demo clean`.
- **Long builds.** A CCS build takes minutes. To keep it running if your terminal closes:
  `nohup ./fw build my_demo > build.log 2>&1 &`.
- **Serial ports.** `ls -l /dev/serial/by-id/` lists them; the by-id names survive replugging.
  Your user needs the `dialout` group. A board is single-user: one flash or capture at a time.
- **File ownership.** The container runs as your user, so everything in `build/` belongs to
  you and can be deleted without `sudo`.
- **flash.sh exit codes.** `0` flashed and confirmed; `1` failed; `2` bad arguments;
  `3` this board has no headless flasher, so the manual steps (UniFlash, jumpers) were printed.

## 5. Walkthrough: add a new project

This creates a project called `zz_demo`; use your own name (lowercase, digits, `_`).
Just trying it out? Skip the `git` lines and delete the folder at the end.

**Step 1. Create it.**

```bash
./fw new zz_demo
./fw list                   # zz_demo appears, with TODO values
```

**Step 2. Add the TI baseline as its own commit.** Copy the TI source you start from into
`src/`, unmodified, and commit it before changing anything. Every later change then shows up
as a diff against that commit. TI sources are inside the image under `/opt/ti`; copy them out
as your user:

```bash
docker compose run --rm --user "$(id -u):$(id -g)" firmware-env \
    cp -r /opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti/demo/xwr18xx/mmw/. \
    /build_context/projects/zz_demo/src/
cd projects/zz_demo/src     # delete TI's prebuilt images and generated docs: not source
rm -rf docs/doxygen *.bin *.map *.xer4f *.xe674 *.rov.xs
cd ../../..
git add projects/zz_demo
git commit -m "Firmware: add zz_demo TI baseline, unmodified source (xwr18xx/mmw, SDK 3.6.2)"
git rev-parse --short HEAD  # the BASELINE_COMMIT for step 3
```

Only source goes in git: never commit TI binaries (images, libraries, executables). Note
what you deleted in the project README, so the baseline can be re-created.

**Step 3. Fill in `project.env`.** Replace every `TODO` value. Set `BASELINE` to the path you
copied in step 2 and `BASELINE_COMMIT` to the hash it printed.

**Step 4. Write `build.sh`.** Replace the block marked `TODO` in `projects/zz_demo/build.sh`.
Keep its three rules: it sources `project.env`, it works from its own folder, and it writes
only to `build/`. TI makefiles and CCS write objects next to their sources, so copy `src/`
into `build/` and build there. For an SDK 3.x demo:

```bash
pushd "${MMWAVE_SDK_PATH}/packages/scripts/unix" > /dev/null
set +u; source ./setenv.sh; set -u     # TI's setenv.sh must be sourced from its own folder
popd > /dev/null
export MMWAVE_SDK_DEVICE=iwr18xx       # after setenv.sh, which sets its own default
rm -rf build/src && cp -r src build/src
make -C build/src all
cp build/src/xwr18xx_mmw_demo.bin build/
```

Don't call `git` in `build.sh` (the repo metadata isn't visible in the container); `fw`
passes the commit in as `FW_COMMIT`.

**Step 5. Build.**

```bash
./fw build zz_demo
ls -l projects/zz_demo/build/   # your ARTIFACTS + build_info.txt
```

**Step 6. Write `flash.sh`.** If the board has a command-line flasher, call it in the `TODO`
block and exit `0` only when it reports success. If not, leave the template's behaviour:
print the manual steps and exit `3`.

**Step 7. Document and commit.** Fill in the project `README.md` (what changed vs TI, status,
board setup), then commit:

```bash
git add projects/zz_demo
git commit -m "Firmware: zz_demo build and flash scripts"
```

To throw the example away instead: `rm -rf projects/zz_demo`.

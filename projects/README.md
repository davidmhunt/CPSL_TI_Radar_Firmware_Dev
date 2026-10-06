# Firmware projects: how to build, flash and add one

Each folder in `projects/` is one self-contained firmware: its source, its chirp configs,
its build and flash scripts, and its docs. The `./fw` script at the top of `firmware_dev/`
builds and flashes any of them inside the Docker build environment, so you never install a
TI SDK on your own machine.

All commands below run from `firmware_dev/`.

## 1. One-time setup

You need a Linux machine with Docker (with `docker compose`) and about 25 GB of disk. The
scripts are tested on Linux only (they use `/dev` serial passthrough and host user IDs).

`firmware_dev/` is an opt-in submodule of the parent repo `CPSL_TI_Radar`. On a fresh clone it
is empty; fetch it first:

```bash
git submodule update --init --checkout firmware_dev     # run from the parent repo root
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
├── tools/                # scripts any project can use: cascade_serial_check.py, md_to_pdf.py
├── Dockerfile, docker-compose.yaml   # the build environment, shared by every project
└── downloads/            # TI installers (download.sh is tracked; the installers are not)
```

**Per project:** anything that belongs to one firmware: source, configs, build and flash
steps, notes. Changing one project can never break another.
**Shared:** the toolchain image and `tools/`, because every project uses the same TI
installs and the same serial protocol. A script used by one project only goes in that
project's `tools/`.

Projects today (`./fw list` shows them with board, SDK and baseline):

| Project | What it is |
|---------|------------|
| [`awr2243_cascade_ddm`](awr2243_cascade_ddm/README.md) | AM273x + AWR2243 two-chip cascade DDM demo (MCU+ SDK, CCS headless build) |
| [`ti_stock_demos`](ti_stock_demos/README.md) | TI's unmodified SDK 3.6 IWR1843 / IWR6843 `mmw` demos, built out of tree |
| [`iwr1843_sar_lvds`](iwr1843_sar_lvds/README.md) | IWR1843 SDK 3.6 `xwr18xx/mmw` demo copied into `src/`; base for the SAR / LVDS firmware |

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
| `BASELINE` | TI source the project started from: path relative to `/opt/ti` (inside the image) | `mmwave_sdk_03_06_02_00-LTS/packages/ti/demo/xwr18xx/mmw` |
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
- **Long builds.** A CCS build takes minutes. To keep it running if your terminal closes, detach
  it: `setsid nohup ./fw build my_demo > build.log 2>&1 &` (`setsid` also survives a
  process-group or cgroup kill, which plain `nohup` does not).
- **Serial ports.** `ls -l /dev/serial/by-id/` lists them; the by-id names survive replugging.
  Your user needs the `dialout` group. A board is single-user: one flash or capture at a time.
- **File ownership.** The container runs as your user, so everything in `build/` belongs to
  you and can be deleted without `sudo`.
- **Commit stamp.** `fw` sets `FW_COMMIT` to the short hash of `firmware_dev` HEAD, with `-dirty`
  appended if `git status --porcelain` lists anything (modified or untracked files).
  `build_info.txt` records it.
- **Same path, same hash.** Compilers embed the in-container build path
  (`/build_context/projects/<project>/build/...`) in the binaries, so two builds give the same
  hash only for the same project folder name. Hashes recorded before this layout (for
  example the 29 Sep cascade build, 427998 B) will not match a build here (428030 B); that is
  not a regression.
- **flash.sh exit codes.** `0` flashed and confirmed; `1` failed; `2` bad arguments;
  `3` the flasher is missing (or, for boards without a headless flasher, the manual steps were printed).
  `iwr1843_sar_lvds` flashes with UniFlash's DSLite behind typed-confirmation gates (UNTESTED on a board; see its README).
- **Flashing a board.** Boards boot from jumpers: the IWR boards use the SOP jumpers (flashing
  mode vs functional mode, flashed with TI's UniFlash GUI, or for `iwr1843_sar_lvds` DSLite via `./fw flash`, UNTESTED on a board); the cascade EVM uses jumper J6
  (bottom two pins = UART flash mode, top two = QSPI run mode; change only with power off).
  The exact steps are in each project README: [`awr2243_cascade_ddm`](awr2243_cascade_ddm/README.md),
  [`ti_stock_demos`](ti_stock_demos/README.md), [`iwr1843_sar_lvds`](iwr1843_sar_lvds/README.md).

## Troubleshooting

- **`docker compose build` fails early** (a missing `COPY` source or installer error): the TI
  installers are not in `downloads/`. Run `./downloads/download.sh` first and check it
  completed.
- **`./fw: no project ...` or empty `firmware_dev/`**: initialize the submodule (section 1).

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
# Delete TI's prebuilt images and generated docs (not source). This list is the
# mmw example; other TI folders need a different list.
cd projects/zz_demo/src
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
into `build/` and build there. (The container sets `MMWAVE_SDK_PATH` to the installed SDK.) For an SDK 3.x demo:

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

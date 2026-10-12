# awr2243_cascade_ddm

## Purpose

Firmware for the AWR2243-2X-CAS-EVM, the 2-chip cascade radar (AM273x + two AWR2243). It uses DDM
(Doppler-division multiplexing) processing and is built with CCS from the TI Radar Toolbox lab source.
It sends a point cloud over the data UART; the host driver configures it over the CLI UART.

## Status

`bench`: our build is flashed and passes one configured run (bench record in `project.toml`; see `./fw list`).

## Build

`./fw build awr2243_cascade_ddm` (run from `firmware_dev/`; about 15 minutes; `CCS_CONFIG=Debug` for a debug build).
Outputs in `build/`: `am273x_cascade.appimage` (the flash image), `am273x_cascade.elf`, `am273x_cascade_dss.xe66`,
`build_info.json`. How the build works: [`docs/cascade_demo_guide.md`](docs/cascade_demo_guide.md).

## Test

`./fw test awr2243_cascade_ddm` (no hardware): manifest and descriptor checks, every `configs/cascade_*.cfg`
against the descriptor limits, and the `uart_uniflash.py` patch check (only when the `firmware-env` image exists).

## Flash

`./fw flash awr2243_cascade_ddm /dev/serial/by-id/<...>-if00` (add `--dry-run` to flash nothing; add `prebuilt`
after the port for TI's own image, from a terminal only).
- Jumper J6: bottom two pins = UART flash mode, top two pins = QSPI run mode. Change it only with power off.
- Power-cycle (12 V off, on) before flashing. Success is `All commands from config file are executed`.
- After flashing, move J6 to the top two pins and power-cycle.
- The radar accepts one chirp cfg per power-up: power-cycle before every configured run.

## Verify

`./fw verify awr2243_cascade_ddm --port <by-id -if00 port>` only listens and reports `skipped`: the cascade
takes one cfg per power-up, so `fw` sends nothing. To check the firmware, run the configured check in
[`docs/bench_check.md`](docs/bench_check.md).

## Bench check

The checks to run on a real board are in [`docs/bench_check.md`](docs/bench_check.md).

## Layout

- `project.toml`: the manifest `./fw` reads
- `build.sh`, `flash.sh`: run in the container
- `src/`: CCS projectspecs and demo/library sources (Radar Toolbox baseline plus our edits)
- `configs/`: chirp cfgs `cascade_*.cfg`
- `prebuilt_binaries/`: TI's two SBL images that `flash.sh` needs
- `tools/`: `cascade_serial_check.py` (manual bring-up check)
- `docs/`: [`cascade_demo_guide.md`](docs/cascade_demo_guide.md) (build, source tree, UART packet format), `bench_check.md`, `archive/`

## Changes vs TI

Baseline: Radar Toolbox 4.00.00.05, `source/ti/examples/Automotive_ADAS_and_Parking/mmwave_2_chip_cascade/src/awr2243`
(`project.toml` `[source]`). `src/` is that tree plus our edits; the first commit that tracks it (`704930d`)
already includes them, so there is no unmodified baseline in git. To see the edits, diff `src/` against the
Radar Toolbox in the image (`/opt/ti/radar_toolbox_4_00_00_05/...`). `configs/`, `docs/`, `tools/` and the
build/flash scripts are not from TI.

TI references (not stored): the Two Chip Cascade user guide (in the Radar Toolbox) and the EVM user's guide, SWRU639.

## Known limits

- A new board may fail to flash if the QSPI Quad Enable bit is unset ("Possible Flashing Issues" in TI's cascade user
  guide: rebuild `sbl_uart_uniflash` with "Quad Enable Type" = 6).
- The prebuilt libraries (`.aer5f`, `.ae66`) are not in git; `build.sh` takes them from the Radar Toolbox.
- Open licensing item (user decision): may the TI SBL images in `prebuilt_binaries/` be shipped publicly?
- The image has no raw-ADC (LVDS) stream; see the guide, section 7.

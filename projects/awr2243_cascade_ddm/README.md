# awr2243_cascade_ddm

## Purpose

Firmware for the AWR2243-2X-CAS-EVM, the 2-chip cascade radar (AM273x + two AWR2243). It uses DDM
(Doppler-division multiplexing) processing and is built with CCS from the TI Radar Toolbox lab source. The AM273x runs
two programs: the MSS (Cortex-R5F: CLI, UART output) and the DSS (C66x DSP: radar processing). It sends a point cloud
as TLV (type-length-value) records over the data UART; the host configures it over the CLI UART.
Glossary: [`fw_cli.md`](../../docs/reference/fw_cli.md#glossary).

## Status

`bench`: our build is flashed and passes one configured run (bench record in `project.toml`; see `./fw list`).

## Build

`./fw build awr2243_cascade_ddm` (run from `firmware_dev/`; about 15 minutes; `CCS_CONFIG=Debug` for a debug build).
Outputs in `build/`: `am273x_cascade.appimage` (the MSS and DSS programs joined into the image to flash), `am273x_cascade.elf`, `am273x_cascade_dss.xe66`,
`build_info.json`. How the build works: [`docs/cascade_demo_guide.md`](docs/cascade_demo_guide.md).

## Test

`./fw test awr2243_cascade_ddm` (no hardware): manifest and descriptor checks, every `configs/cascade_*.cfg` against the
descriptor limits, the `uart_uniflash.py` patch check (only when the `firmware-env` image exists).

## Flash

`./fw flash awr2243_cascade_ddm /dev/serial/by-id/<...>-if00` (add `--dry-run` to flash nothing; add `prebuilt`
after the port to flash TI's own appimage instead of ours, from a terminal only).
- Jumper J6: bottom two pins = UART flash mode, top two pins = QSPI run mode. Change it only with power off.
- Power-cycle (12 V off, on) before flashing. Success is `All commands from config file are executed`.
- After flashing, move J6 to the top two pins and power-cycle.
- The radar accepts one chirp cfg per power-up: power-cycle before every configured run.

What is written (`flash.sh:55-69`): TI's SBL (secondary bootloader) `prebuilt_binaries/sbl_uart_uniflash.release.tiimage`
is passed as `--flash-writer`; then `sbl_qspi.release.tiimage` (the QSPI-boot SBL) goes to flash offset 0x0 and the
appimage to 0xA0000. `prebuilt` swaps only the appimage, for the Radar Toolbox's `am273x_mmw_cascade_demo_DDM.appimage`.

## Verify

`./fw verify awr2243_cascade_ddm --port <by-id -if00 port>` only listens and reports `skipped`: the cascade
takes one cfg per power-up, so `fw` sends nothing. To check the firmware, run the configured check in
[`docs/bench_check.md`](docs/bench_check.md).

## Bench check

The checks to run on a real board are in [`docs/bench_check.md`](docs/bench_check.md).

## Layout

- `project.toml`: the manifest `./fw` reads; `build.sh`, `flash.sh`: run in the container
- `src/`: CCS projectspecs and demo/library sources; `configs/`: chirp cfgs `cascade_*.cfg`
- `prebuilt_binaries/`: TI's two SBL images; `tools/`: `cascade_serial_check.py` (manual bring-up check)
- `docs/`: [`cascade_demo_guide.md`](docs/cascade_demo_guide.md) (build, source tree, UART packet format), `bench_check.md`, `archive/`

## Changes vs TI

Baseline: Radar Toolbox 4.00.00.05, `source/ti/examples/Automotive_ADAS_and_Parking/mmwave_2_chip_cascade/src/awr2243`
(`project.toml` `[source]`). `src/` is that tree plus our edits; git holds no unmodified baseline, so diff `src/` against the
Radar Toolbox in the image (`/opt/ti/radar_toolbox_4_00_00_05/...`). `configs/`, `docs/`, `tools/` and the scripts are not
from TI. TI references (not stored): the Two Chip Cascade user guide (in the Radar Toolbox) and the EVM user's guide, SWRU639.

## Known limits

- A new board may fail to flash if the QSPI Quad Enable bit is unset ("Possible Flashing Issues" in TI's cascade user
  guide: rebuild `sbl_uart_uniflash` with "Quad Enable Type" = 6).
- The prebuilt libraries (`.aer5f`, `.ae66`) are not in git; `build.sh` takes them from the Radar Toolbox.
- Unresolved: whether the TI SBL images in `prebuilt_binaries/` may be shipped publicly.
- The image has no raw-ADC (LVDS) stream; see the guide, section 7.

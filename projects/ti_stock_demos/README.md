# ti_stock_demos

## Purpose

TI's unmodified mmWave SDK 3.6.02.00-LTS `mmw` demos for the IWR1843 (`xwr18xx`) and the IWR6843 (`xwr68xx`),
built out of tree: nothing is written into `/opt/ti`.

## Status

`bench`: the IWR1843 image has a passing `[[bench]]` record; the IWR6843 image has none (see `./fw list`).

## Build

`./fw build ti_stock_demos` from `firmware_dev/` (about 10 minutes; builds both families), or add `--variant 18xx` /
`--variant 68xx` for one. A build wipes `build/`, so a one-variant build removes the other family's outputs.
Outputs in `projects/ti_stock_demos/build/`: `iwr1843_demo.bin`, `iwr6843_demo.bin` (flash images), the matching `.elf`
files, `*_demo_{mss,dss}.map`, `build_info.txt`, `compilers.txt`, `build_info.json`. How the build works: [`docs/reference/build_environment.md`](../../docs/reference/build_environment.md).

## Test

`./fw test ti_stock_demos`: hardware-free checks (manifest artifacts, script interface, every `configs/xwr*/*.cfg` against the board's `cfg_rules`).

## Flash

`./fw flash ti_stock_demos <by-id -if00 port> [image] [--dry-run]`. You type `FLASH MODE CONFIRMED`; `--dry-run` shows
the command and image sha256 with no board. Terms: [glossary](../../docs/reference/fw_cli.md#glossary).

Boot mode is the S1 switch (SOP2, SOP1, SOP0, left to right, ON = up; set it with power off). **SOP 101** = flash mode
(SOP2 and SOP0 ON, SOP1 OFF); **SOP 001** = run mode (SOP0 ON only). Switch positions are checked on the IWR1843BOOST only.

1. Set SOP 101, then power-cycle (USB + 5 V). Repeat before EVERY attempt.
2. Success = `SUCCESS!! File type META_IMAGE1`.
3. Set SOP 001, power-cycle, then send a `configs/xwr*/profile_*.cfg` over the CLI port (115200 baud). A cfg
   can be re-sent without a power-cycle (only the cascade is once per power-up).

The IWR1843 image is flashed by UniFlash 9.6.0's DSLite (`configs/iwr1843_uniflash.ccxml`). The IWR6843 has no headless
path: `./fw flash` prints the steps and exits 3. By hand: SOP 101, power on; in the UniFlash GUI pick device IWR6843, the
board's serial port, Meta Image 1 = `build/iwr6843_demo.bin`, Format = "bin"; flash; then SOP 001 and power-cycle.

## Verify

`./fw verify ti_stock_demos --port <by-id port>` sends `version` (and, on the IWR1843, `sarStats`, which this image must
reject as "not recognized", unlike the SAR image). Safe: no cfg command is sent.

## Bench check

The checks to run on a real board: [`docs/bench_check.md`](docs/bench_check.md).

## Layout

`project.toml`, `build.sh`, `flash.sh`; `configs/` (the ccxml, TI's `xwr18xx/`, `xwr68xx/` profiles); `src/`, `tools/` are empty.

## Changes vs TI

Nothing; `[source]` in `project.toml` names the SDK folders built. To modify a demo, start a new project (`./fw new`).

## Known limits

- Headless flashing is IWR1843 only; the IWR6843 image is flashed by hand (no ccxml for it).
- The IWR6843 image has never been run on a board.

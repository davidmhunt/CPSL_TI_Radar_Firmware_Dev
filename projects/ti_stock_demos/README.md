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

`./fw test ti_stock_demos` runs the hardware-free checks plus `tests/test_project.py` (manifest artifacts, the `FW_*`
interface of the scripts, every `configs/xwr*/*.cfg` meets the board's `cfg_rules`).

## Flash

`./fw flash ti_stock_demos <by-id -if00 port> [image] [--dry-run]`. You type `FLASH MODE CONFIRMED`; `--dry-run` shows
the command and image sha256 with no board. Scripts use `--plan` then `--confirm <token>`
([reference](../../docs/reference/fw_cli.md)).

1. Flash mode (SOP 101, SOP0 + SOP2) with power off, then power-cycle (USB + 5 V). Repeat before EVERY attempt.
2. Success = `SUCCESS!! File type META_IMAGE1`.
3. Run mode (SOP0 only), power-cycle, then send a `configs/xwr*/profile_*.cfg` over the CLI port (115200 baud). A cfg
   can be re-sent without a power-cycle (only the cascade is once per power-up).

The IWR1843 image uses UniFlash 9.6.0's DSLite (`configs/iwr1843_uniflash.ccxml`). For `iwr6843_demo.bin`, `flash.sh`
prints the UniFlash GUI steps and exits 3: flash mode SOP0 + SOP2, Format = "bin", then run mode.

## Verify

`./fw verify ti_stock_demos --port <by-id port>` sends `version` (plus `sarStats` on the IWR1843, to tell this image from
the SAR image). Safe: no cfg command is sent. A good answer is the `version` reply.

## Bench check

The checks to run on a real board: [`docs/bench_check.md`](docs/bench_check.md).

## Layout

- `project.toml`, `build.sh`, `flash.sh`: the manifest and the scripts run in the container
- `configs/`: `iwr1843_uniflash.ccxml`; `xwr18xx/`, `xwr68xx/` hold TI's `profiles/*.cfg` and the config-update `.pl` helper
- `src/` and `tools/` are empty (no TI source is tracked)

## Changes vs TI

Nothing; `[source]` in `project.toml` names the SDK folders built. To modify a demo, start a new project (`./fw new`).

## Known limits

- Headless flashing is IWR1843 only; the IWR6843 image is flashed by hand (no ccxml for it).
- The IWR6843 image has never been run on a board.

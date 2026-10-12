# ti_stock_demos

## Purpose

TI's unmodified mmWave SDK 3.6.02.00-LTS `mmw` demos for the IWR1843 (`xwr18xx`) and the IWR6843 (`xwr68xx`),
built out of tree: nothing is written into `/opt/ti`.

## Status

Quote `./fw list ti_stock_demos` (status `bench`: the IWR1843 image has a passing `[[bench]]` record in `project.toml`;
the IWR6843 image has none). Numbers, hashes and dates live in `project.toml` and [`docs/RESULTS.md`](../../../docs/RESULTS.md) (parent repo), not here.

## Build

From `firmware_dev/`: `./fw build ti_stock_demos` (checks `./fw deps` first; builds both families) or
`--variant 18xx` / `--variant 68xx` for one. Outputs in `projects/ti_stock_demos/build/`: `iwr1843_demo.bin`,
`iwr6843_demo.bin` (flash images), `iwr1843_demo.elf`, `iwr6843_demo.elf` (MSS executables, `.xer4f`),
`*_demo_{mss,dss}.map`, `build_info.txt`, `compilers.txt`, `build_info.json` (written by `fw`, sha256 of each artifact).
A build wipes `build/` first, so a one-variant build removes the other family's outputs.

The demo is built out of tree in an SDK overlay under `build/sdk/` (nothing is written to `/opt/ti`); how the
overlay works, the compilers used and why the `.bin` size depends on the build path:
[`docs/reference/build_environment.md`](../../docs/reference/build_environment.md).

## Test

`./fw test ti_stock_demos` runs the hardware-free checks (manifest, descriptor back-links, cfg globs, headings,
`build --dry-run`) and `tests/test_project.py` (stdlib unittest): the manifest names the four artifacts and their `demo`
descriptor entries; `build.sh` / `flash.sh` read the standard `FW_*` interface; every tracked `configs/xwr*/*.cfg` parses and
satisfies the descriptor's `cfg_rules` for its board (`calibData` for the IWR1843 profiles).

## Flash

`./fw flash ti_stock_demos <by-id -if00 port> [image] [--dry-run]` (a human types `FLASH MODE CONFIRMED`), or for scripts
`--plan` then `--confirm <token>`. IWR1843 images use UniFlash 9.6.0's DSLite with `configs/iwr1843_uniflash.ccxml`, exactly as
`iwr1843_sar_lvds` does. Put the board in SOP 101 (SOP0 + SOP2) and power-cycle (USB + 5 V) before every attempt; add
`--dry-run` to see the command and image sha256 with no board. Success = `SUCCESS!! File type META_IMAGE1`. After flashing, set
functional mode (SOP0 only), power-cycle, and send a `configs/xwr*/profile_*.cfg` over the CLI port (115200 baud); a cfg can be re-sent without a
power-cycle (only the cascade is once per power-up). `flash.sh` has no headless flasher for `iwr6843_demo.bin`: it prints the UniFlash GUI steps and
exits 3 (flashing mode SOP0 + SOP2, Format = "bin", then functional mode).

## Verify

`./fw verify ti_stock_demos [--port P]` sends the `demo` descriptor's `identify` probes for the board (`version`, plus `sarStats`
for the IWR1843 to tell this image from the SAR image). Safe to probe: none of the probes is a cfg command.

## Bench check

The user-run check is `docs/bench_check.md` (flash the IWR1843 image, SOP 001, `mmwDemo:/>` and `version`); results are
recorded as `[[bench]]` in `project.toml`.

## Layout

| Path | What |
|------|------|
| `project.toml` | the manifest |
| `build.sh`, `flash.sh` | run in the container; read the standard `FW_*` environment |
| `configs/` | `iwr1843_uniflash.ccxml`; `xwr18xx/`, `xwr68xx/`: copies of TI's `profiles/*.cfg` and the config-update `.pl` helper (no license header; the SDK software manifest lists them as BSD-3-Clause) |
| `docs/` | `bench_check.md` |
| `tests/` | hardware-free host tests |
| `src/`, `tools/` | empty (stock project: no TI source is tracked) |

## Changes vs TI

Nothing. `src/` is empty and no TI source is tracked; `[source]` in `project.toml` names the SDK folders that are built
(`packages/ti/demo/xwr{18,68}xx/mmw`, inside the image at `/opt/ti/mmwave_sdk_03_06_02_00-LTS`). To modify a demo, start a new
project (`./fw new`) and copy the demo folder into its `src/` (`iwr1843_sar_lvds` does this).

## Known limits

- Headless flashing is IWR1843 only; the IWR6843 image is flashed by hand (no ccxml for it).
- The IWR6843 image has never been run on a board; its descriptor level is `source`-derived.

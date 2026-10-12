# iwr1843_sar_lvds

## Purpose

IWR1843BOOST image for synthetic-aperture capture, derived from the TI mmWave SDK 3.6.02.00-LTS `xwr18xx/mmw` demo: MSS-only,
configured over the CLI, streaming the raw ADC data of every chirp over LVDS to a DCA1000 with 64 B of metadata per chirp (two
32-byte record slots). The driver side is the `IWR1843_SAR` board in the parent repo.

## Status

`./fw list`: `iwr1843_sar_lvds  bench  IWR1843  mmwave_sdk 03.06.02.00-LTS`. Bench records: `project.toml` `[[bench]]`.

## Build

From `firmware_dev/`: `./fw build iwr1843_sar_lvds` (about 10 minutes; prerequisites: [`../../README.md`](../../README.md)) writes `build/` (`iwr1843_sar_lvds.bin` flash image,
`.elf`, map, `build_info.*`). No `--variant`. SDK overlay and sizes: [`docs/build_flash_notes.md`](docs/build_flash_notes.md).

## Test

`./fw test iwr1843_sar_lvds`: the 7 host test files `tools/test_*.py` (synthetic captures; the report and sweep tests need
`uv sync --group tools`), the manifest checks, and `tools/sar_cfg_check.py` over `configs/sar_example_2ms.cfg` and the parent's SAR cfg.

## Flash

`./fw flash iwr1843_sar_lvds <by-id -if00 port>` (UniFlash DSLite; `--dry-run` prints the command and image sha256, touches no port).
Flash mode: SOP0 + SOP2 closed (SOP 101), set with power off. Power-cycle (USB + 5 V) before every attempt, retries included, and
type `FLASH MODE CONFIRMED` at a TTY. Run mode: SOP0 only (SOP 001); power-cycle once after flashing, then send a cfg at 115200 baud
(no power-cycle needed between cfgs). Step by step: [`docs/bench_bringup.md`](docs/bench_bringup.md) section 3; stock-demo restore and
failure messages: [`docs/build_flash_notes.md`](docs/build_flash_notes.md).

## Verify

`./fw verify iwr1843_sar_lvds --port <by-id -if00 port>` sends `version` and `sarStats` (no cfg, safe to probe); expects the prompt `mm_sar_lvds:/>`.

## Bench check

Short sequence (flash, verify, one capture): [`docs/bench_check.md`](docs/bench_check.md). Full procedure, with the no-reflector
and reflector runs: [`docs/bench_bringup.md`](docs/bench_bringup.md).

## Layout

`src/` (edited TI demo source), `configs/` (`sar_example_2ms.cfg`, the DSLite ccxml), `tools/` (host tools, tests, `./bench`
runners), `build/` (gitignored). Docs, Use:


- Design a cfg: [`sar_cfg_guide.md`](docs/sar_cfg_guide.md); checker `tools/sar_cfg_check.py`.
- Capture, parse, tune gain/HPF: [`tuning_guide.md`](docs/tuning_guide.md).


Reference: [`lvds_data_format.md`](docs/lvds_data_format.md) (packet and metadata format), [`build_flash_notes.md`](docs/build_flash_notes.md) (troubleshooting), [`changes_vs_ti.md`](docs/changes_vs_ti.md),
[`lvds_code_map.md`](docs/lvds_code_map.md) (source map), [`sar_feasibility.md`](docs/sar_feasibility.md) (limits, go/no-go).

## Changes vs TI

Baseline: `project.toml` `[source]`; every change since: `git diff bb3a348 -- projects/iwr1843_sar_lvds/src`. Detail and license
notes: [`docs/changes_vs_ti.md`](docs/changes_vs_ti.md).

- DSP chain removed: MSS-only, no TLV or data UART; the CLI commands that fed it (`guiMonitor`, `cfarCfg`, ...) are gone,
  so a stock TI cfg fails on the first of them.
- Frame mode only (`dfeDataOutputMode 1`); the CBUFF session stays active across frames.

- Restart without a power cycle: `sensorStop`, `flushCfg`, a full cfg, `sensorStart`. `channelCfg`, `adcCfg` and `lowPower`
  apply on the first start only (a later different value is rejected). Periodic runtime calibration is off.
- `lvdsStreamCfg` dataFmt 2: 64 B of metadata per chirp (frame and chirp index, timestamp at 100 MHz, saturation of an earlier
  chirp with its lag), and the `sarStats` CLI command.

## Known limits

- Not yet bench-verified (reflector runs): phase continuity, gain/HPF point, boundary Tb, ADC full scale, I/Q order, saturation vs gain.
- `dca_capture.py` and the sweep default to the factory DCA1000 address (as does the parent's `IWR1843_SAR` config); other addresses: [`docs/tuning_guide.md`](docs/tuning_guide.md) section 3.

# iwr1843_sar_lvds

## Purpose

IWR1843BOOST image for synthetic-aperture capture, derived from the TI mmWave SDK 3.6.02.00-LTS `xwr18xx/mmw` demo and
built out of tree: MSS-only (no DSS image), configured over the CLI, streaming the raw ADC data of every chirp over
LVDS to a DCA1000 with 64 B of metadata per chirp (two 32-byte record slots); no on-chip processing, no TLV output. The driver side is the
`IWR1843_SAR` board in the parent repo.

## Status

`./fw list`: `iwr1843_sar_lvds  bench  IWR1843  mmwave_sdk 03.06.02.00-LTS`. Bench records: `project.toml` `[[bench]]`.

## Build

From `firmware_dev/` (prerequisites: top-level [`../../README.md`](../../README.md)): `./fw build iwr1843_sar_lvds`
writes `projects/iwr1843_sar_lvds/build/` (`iwr1843_sar_lvds.bin` flash image, `.elf`, map, `build_info.txt`, `build_info.json`). No
`--variant`. Every build is a clean build of the current `src/`; how the SDK overlay works and the size facts are in
[`docs/build_flash_notes.md`](docs/build_flash_notes.md) and [`docs/changes_vs_ti.md`](docs/changes_vs_ti.md).

## Test

`./fw test iwr1843_sar_lvds`: the 7 host test files in `tools/test_*.py` (synthetic captures, no hardware: parser,
checks 1-4, cfg checker, tuning report and sweep), the manifest and descriptor checks, and `tools/sar_cfg_check.py`
over `configs/sar_example_2ms.cfg` and the parent's SAR cfg. The report and sweep tests need `uv sync --group tools`.

## Flash

`./fw flash iwr1843_sar_lvds <by-id -if00 port>` (UniFlash DSLite; `--dry-run` prints the checklist, command and image sha256, and touches no port).
Put the board in flash mode (SOP0 + SOP2 closed, set with power off), power-cycle (USB + 5 V) before every attempt,
retries included, and type `FLASH MODE CONFIRMED` at a TTY. Return to run mode: SOP0 only,
power-cycle (the one needed after flashing), then send a cfg at 115200 baud (no further power-cycle between cfgs, see Changes vs TI). Details, the stock-demo restore and failure messages:
[`docs/build_flash_notes.md`](docs/build_flash_notes.md).

## Verify

`./fw verify iwr1843_sar_lvds --port <by-id -if00 port>` sends the descriptor's `version` and `sarStats` probes (no
cfg, safe to probe) and expects the prompt `mm_sar_lvds:/>`.

## Bench check

[`docs/bench_check.md`](docs/bench_check.md) is the short gated sequence (flash, verify, one capture);
[`docs/bench_bringup.md`](docs/bench_bringup.md) is the full procedure, including the runs without a reflector (Set A)
and with a corner reflector at a measured range (Set B).

## Layout

| Path | What |
|------|------|
| `project.toml`, `build.sh`, `flash.sh` | the manifest; scripts run in the container |
| `src/` | the TI demo source, edited |
| `configs/` | `sar_example_2ms.cfg`, the DSLite ccxml |
| `tools/` | host tools and their tests: capture, parse, tune ([`docs/capture_parse_tune.md`](docs/capture_parse_tune.md)); bench runners `bench_check.py`, `bench_run.py`, `bench_a.py`, `bench_b.py` (`./bench <check>` wrapper in `firmware_dev/`, see `bench_bringup.md`) |
| `docs/` | notes, indexed below |
| `build/` | outputs, gitignored |

Docs index:

- Design a cfg: [`docs/sar_cfg_guide.md`](docs/sar_cfg_guide.md) (command reference, requirements to values, timing,
  HPF/gain, worked example, common mistakes); example `configs/sar_example_2ms.cfg`, checker `tools/sar_cfg_check.py`.
  Limits and go/no-go criteria: [`docs/sar_feasibility.md`](docs/sar_feasibility.md).
- Capture and parse: commands in [`docs/tuning_guide.md`](docs/tuning_guide.md) section 3, tool table in
  [`docs/capture_parse_tune.md`](docs/capture_parse_tune.md), packet and metadata format in
  [`docs/lvds_data_format.md`](docs/lvds_data_format.md).
- Tune: [`docs/tuning_guide.md`](docs/tuning_guide.md) (the capture requirement, `sarStats`, the four checks, gain and
  HPF procedure, sweep table).
- Source map: [`docs/lvds_code_map.md`](docs/lvds_code_map.md).
- Troubleshooting: [`docs/build_flash_notes.md`](docs/build_flash_notes.md).

## Changes vs TI

Baseline: `project.toml` `[source]`; every change since: `git diff bb3a348 -- projects/iwr1843_sar_lvds/src` (`baseline_commit`).
Detail, with the license notes for the TI sources: [`docs/changes_vs_ti.md`](docs/changes_vs_ti.md). In short:

- DSP chain removed: MSS-only image, no DSS, no TLV or data UART; the CLI commands that only fed the object-detection
  chain (`guiMonitor`, `cfarCfg`, ...) are gone, so a stock TI cfg fails on the first of them.
- Frame mode only (`dfeDataOutputMode 1`); the CBUFF hardware session is created before the first chirp and stays
  active across frame boundaries.
- Restart without a power cycle: `sensorStop`, `flushCfg`, a full cfg, `sensorStart`. `channelCfg`, `adcCfg` and
  `lowPower` apply on the first start only (a later different value is rejected).
- Periodic runtime calibration off.
- `lvdsStreamCfg` dataFmt 2: 64 B of metadata per chirp, two 32-byte record slots (frame and chirp index, timestamp at 100 MHz,
  saturation of an earlier chirp with its lag), and the `sarStats` CLI command.

## Known limits

- The reflector tuning checks (Set B: phase continuity, gain/HPF point, boundary Tb, ADC full scale, I/Q order,
  saturation vs gain) are not yet bench-verified; they are pending firmware-18.
- `dca_capture.py` and the sweep default to the factory DCA1000 address (the parent's `IWR1843_SAR` config uses the same); other
  addresses: flags in [`docs/capture_parse_tune.md`](docs/capture_parse_tune.md).

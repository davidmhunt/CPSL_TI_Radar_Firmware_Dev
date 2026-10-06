# iwr1843_sar_lvds

IWR1843BOOST SAR / LVDS raw-ADC firmware, derived from the TI mmWave SDK 3.6.02.00-LTS `xwr18xx/mmw`
demo and built out of tree. It is an MSS-only image (MSS + BSS firmware, no DSS image) that configures the
radar from the CLI and streams the ADC data of every chirp over LVDS to a DCA1000.

## What changed vs TI

Baseline: `project.env` (`BASELINE`, `BASELINE_COMMIT`). See every change since with
`git diff <BASELINE_COMMIT> -- projects/iwr1843_sar_lvds/src`.

**firmware-07: DSP chain removed, MSS-only, restartable, periodic calibration off.**

- **Removed** (TI's object-detection chain and everything that only served it):
  - `src/dss/` (DSS image sources) and `src/include/mmw_output.h` (UART TLV format);
  - from `mss/mss_main.c`: DPM/DPC objdet init, ioctls, report handler and DPM task; per-frame result
    handling and its waits on DSS results and on the LVDS frame-done semaphores; UART TLV output and the
    data-UART open; the per-frame LVDS SW session (point cloud); HSRAM shared-memory buffer; per-sub-frame
    reconfiguration (advanced frame);
  - from `mss/mmw_lvds_stream.{c,h}`: the SW session (EDMA table, config, delete, callback, user header) and
    the frame-done semaphores; `mmw_res.h`: the DPC/HWA EDMA partitioning and SW-session channels;
    `include/mmw_config.h` / `mss/mmw_mss.h`: GUI-monitor and DPC config types, DPM handles;
  - CLI commands that only fed the DPC or the TLV output: `guiMonitor`, `cfarCfg`, `multiObjBeamForming`,
    `calibDcRangeSig`, `clutterRemoval`, `compRangeBiasAndRxChanPhase`, `measureRangeBiasAndRxChanPhase`,
    `aoaFovCfg`, `cfarFovCfg`, `extendedMaxVelocity`, `configDataPort`. A stock TI cfg (including
    `configs/*.cfg`, kept as TI reference profiles) now fails on the first of these.
  - build: `makefile` builds `mmwDemo` = `mssDemo` + `generateMetaImage.sh <bin> $(SHMEM_ALLOC) <mss> <radarss> NULL`
    (`SHMEM_ALLOC` = SDK default `0x00000008`); DSS, AOP and secure (HS) targets dropped; `mmw_mss.mak` no
    longer links `libdpm`; the linker cmd no longer places `.demoSharedMem` in HS_RAM.
- **Kept**: the SDK mmWave CLI extension (channel/adc/profile/chirp/frame cfg, `flushCfg`), `adcbufCfg`
  (complex, chirpThreshold 1), `lvdsStreamCfg` (HW session, dataFmt 0/1/4, header on/off, dataFmt 2 since firmware-08; `enableSW 1`
  rejected), `analogMonitor`, `CQRxSatMonitor`, `CQSigImgMonitor`, `calibData`, `queryDemoStatus`,
  `sensorStart`, `sensorStop`; calibration save/restore to flash; HSI clock 600 Mbps DDR; CLI prompt
  `mmwDemo:/>` (banner now names this firmware).
- **Changed**:
  - DSP left halted (`SOC_DSSCfg_HALT`, as in TI's MSS-only xwr64xx demo); mmWave runs in isolation mode.
  - Only `dfeDataOutputMode 1` (frame) is accepted; advanced frame and continuous mode are rejected at
    `sensorStart`.
  - ADCBUF is opened and configured from the MSS at every sensor start (`MmwDemo_dataPathStart`), the RF
    parser runs on every reconfig (`MmwDemo_configSensor`), and the CBUFF HW session (ADC format, works
    with `numFrames 0`) is created and activated before the first chirp. It stays active across frame
    boundaries; nothing waits on a frame event, so a frame boundary never blocks the next frame.
  - **Restart without a power cycle.** `sensorStop` stops the RF, waits (bounded, 3 s) for the BSS
    frame-end event, then deactivates and deletes the HW session (its EDMA channels and HSI header are
    returned; a leak is reported as an error) and closes ADCBUF. Then `flushCfg`, a full cfg and
    `sensorStart` re-run `MMWave_config` and the RF parser and re-create ADCBUF and the HW session, so
    every profile/chirp/frame/adcbuf/lvds parameter may change, including `rxGain`, the HPF corners, the
    sample count and the sample rate. `sensorStart 0` restarts with the previous cfg. A run with a finite `numFrames` that has finished
    still needs `sensorStop` before the next `sensorStart` (otherwise "Ignored: Sensor is already started"). `channelCfg`,
    `adcCfg` and `lowPower` are applied by `MMWave_open` on the first start only (stock behaviour); a
    later start with different values is rejected with a CLI error (the TI demo halted the MSS instead).
    Changing them needs a power cycle. After `flushCfg` they must be re-sent with the original values.
    A full cfg means the mmWave commands plus `adcbufCfg`, `lvdsStreamCfg`, `analogMonitor` and
    `calibData` (stock rule: all four must be re-sent after a stop for `sensorStart` to reconfigure).
  - **Periodic runtime calibration off**: `MMWave_start` gets `enablePeriodicity = false` (TI: every 10
    frames). Boot calibration (first `MMWave_open`) and TI's one-time runtime calibration at each
    `sensorStart` stay on.
  - Errors in the start path are reported on the CLI and undo the partial setup instead of asserting.

**firmware-08: per-chirp SAR metadata over LVDS (`lvdsStreamCfg` dataFmt 2) and `sarStats`.** Format and host
parsing: [`docs/lvds_data_format.md`](docs/lvds_data_format.md).

- **Added** `mss/mmw_sar_meta.{c,h}`: the 32-byte record (magic, version, flags, frame index, chirp in frame, chirps
  per frame, global chirp index, run index, saturation of an earlier chirp with its lag, 64-bit timestamp from the
  BIOS RTI counter at 100 MHz); two record slots in L3 (`.cbuffL3Memory`, placed by `mss/mmw_mss_linker.cmd`); three
  interrupt handlers: chirp start (VIM 99, own Hwi) fills chirp k's record into slot k mod 2, chirp available (VIM 123,
  SOC listener) counts the saturated CQ2 primary slices, frame start (VIM 98, SOC listener) cross-checks the counters.
  A 1 s BIOS clock function keeps the 64-bit timestamp extension current.
- **Changed**: `lvdsStreamCfg` accepts dataFmt 2 = CBUFF `ADC_USER` with the slots as its user buffer
  (`mss/mmw_lvds_stream.c`); `sensorStart` rejects dataFmt 2 unless the ADC output is complex, ChanInterleave is 1 and
  numAdcSamples × RX is even. `mss/mmw_mss.mak` compiles the SDK's `drivers/cbuff/platform/cbuff_xwr18xx.c` with
  `ENABLE_ALL_NON_INTERLEAVED`, because TI's prebuilt CBUFF library leaves `ADC_USER` out. dataFmt 1 and 4 stream as
  in firmware-07; the interrupt handlers run in every format (they feed `sarStats`).
- **New CLI** `sarStats` (no arguments, works while running, reset at every `sensorStart`): chirps, frames, chirp-start
  and chirp-available interrupts, saturated chirps, late and missed chirp interrupts, counter resyncs, CBUFF error
  interrupts, the sticky CBUFF chirp/frame-start error bits, LVDS frames done, current timestamp.

The baseline was copied verbatim from `packages/ti/demo/xwr18xx/mmw/` of the SDK: `mss/ dss/ include/ makefile
mmw_res.h` into `src/`, `profiles/*.cfg` into `configs/`, `profiles/mmwDemo_xwr18xx_update_config.pl`
into `tools/`. Left out (not source): `docs/` (generated doxygen) and TI's prebuilt outputs
(`*.bin *.map *.xer4f *.xe674 *.rov.xs`, including the AOP variants).

## LICENSE

The TI sources in `src/`, `configs/` and `tools/` are tracked under TI's own terms. The C sources and
headers carry TI's BSD-3-clause header ("Redistribution and use in source and binary forms ...").
Files without that header fall into two groups:

- **No header at all:** `makefile`, `*.mak`, `*_linker.cmd`, the `*.cfg` chirp profiles and the `.pl` helper.
- **Old header:** the XDC/BIOS config `mss/mmw_mss.cfg` (and `dss/mmw_dss.cfg` until firmware-07 removed it) carries an older
  "Copyright 2011 ... Restricted rights" boilerplate instead of the BSD text.

Both groups are covered by the SDK software manifest (`docs/mmwave_sdk_software_manifest.html`:
"mmwave drivers, control, datapath, utils & demo", `packages\ti\demo`, BSD-3-Clause). Keep these
headers when editing. No TI binaries are tracked; the
open release-gate item on shipping TI binaries publicly is unchanged by this project.

## Docs

- [`docs/lvds_code_map.md`](docs/lvds_code_map.md): where the demo streams ADC data over LVDS and where a custom payload hooks in.
- [`docs/lvds_data_format.md`](docs/lvds_data_format.md): dataFmt 2 packet and metadata record, timestamp, how to align
  the lagged saturation field, host parsing.
- [`docs/sar_cfg_guide.md`](docs/sar_cfg_guide.md): how to build a valid SAR cfg: command reference, requirements to values, timing, HPF/gain, worked example, common mistakes. Example `configs/sar_example_2ms.cfg`; checker `tools/sar_cfg_check.py` (tests: `tools/test_sar_cfg_check.py`).
- [`docs/sar_feasibility.md`](docs/sar_feasibility.md): IWR1843 / SDK 3.6 limits for 1TX/1RX continuous-chirp SAR, data-rate math, frame-boundary rule, go/no-go criteria.
- [`docs/tuning_guide.md`](docs/tuning_guide.md): the capture requirement, `sarStats`, the four capture checks, the bench procedure for gain and HPF corners, how to read the tuning report and sweep table.

## Capture, parse, tune

Host tools in `tools/` (Python; run from `firmware_dev/`; guide: [`docs/tuning_guide.md`](docs/tuning_guide.md)). A recording is only usable if it
holds the run's first byte: arm the DCA1000 before `sensorStart`, stop it after `sensorStop`, read `sarStats` after `sensorStop`.
`sar_parse.py` proves this with checks 1-4 ([`docs/lvds_data_format.md`](docs/lvds_data_format.md) §1) and refuses to write aligned output
for a recording that fails any of them (`--force` writes it for debugging only).

| Tool | Does | Hardware |
|---|---|---|
| `tools/dca_capture.py` | configure and arm the DCA1000, drive `sensorStart` / `sensorStop` / `sarStats` on the CLI port, record datagrams with their UDP headers, store `chirpAvail` beside the recording | DCA1000, radar CLI port |
| `tools/sar_parse.py` | place packets by DCA1000 byte count, run checks 1-4, write `_adc.bin` (int16 I/Q, chirp-major) and `_meta.csv` (saturation lag applied) | none |
| `tools/sar_tune_report.py` | peak dBFS, clipped chirps, noise floor, range profile with the HPF response, reflector SNR; needs the `tools` uv group | none |
| `tools/sar_tune_sweep.py` | per gain / HPF point: cfg check, send cfg, capture, parse, report, one table; no power cycle | DCA1000, radar CLI port |
| `tools/sar_synth.py` | synthetic captures (clean, late, mid-packet, lost datagram, ...) for the tests and for trying the tools | none |

```bash
uv sync --group tools        # numpy + matplotlib, only for the report and sweep
uv run python -m unittest discover -s projects/iwr1843_sar_lvds/tools -p 'test_*.py'
```

Hardware use of these tools is untested until the bench validation (Status below): the tests use synthetic captures only.

## Status

**MSS-only raw-ADC streaming (firmware-07) with per-chirp metadata and saturation (firmware-08).** SAR cfg guide, example cfg and
checker: `docs/sar_cfg_guide.md`. See `docs/sar_feasibility.md` and `docs/lvds_data_format.md`.

- Build (firmware-08, 2026-10-05): `./fw build iwr1843_sar_lvds` exits 0 with no compiler or linker warnings; `.bin`
  151940 B; the map takes the CBUFF format table from the project's `cbuff_xwr18xx.oer4f` and places the record slots
  at L3 `0x51000000` (64 B).

- Build (firmware-07, 2026-10-05): `./fw build iwr1843_sar_lvds` exits 0 with no compiler or linker
  warnings (warnings are errors in the SDK flags); `.bin` 147844 B (TI baseline image 324804 B); the MSS
  map has no `objdet`/`DPM_`/`DPC_` symbol; the metaimage is generated with DSS `NULL` and no `.xe674`
  is built.
- Baseline equivalence (firmware-04): the unmodified `src/` built to the same 324804 B `.bin` as
  `ti_stock_demos`, byte-identical at a path of the same length.
- On-board: not tested. Streaming, restart without a power cycle, the frame-boundary gap and the per-chirp
  metadata (chirp-start interrupt, DCA1000 byte order of the record, timestamp rate and jitter) are checked on the
  bench in firmware-10.

## How the build works

Same out-of-tree technique as `projects/ti_stock_demos` (see its README). `build.sh` deletes
`build/` and recreates an SDK overlay in `build/sdk/`: `packages/` is a symlink farm onto the SDK,
except `ti/demo/xwr18xx/mmw`, which is a fresh copy of this project's `src/` (TI prebuilt outputs
and `docs/` excluded). Every build is a clean build of the current `src/`, so local edits are
always picked up. After sourcing TI's `setenv.sh` it sets `MMWAVE_SDK_DEVICE=iwr18xx` and
`MMWAVE_SDK_INSTALL_PATH=build/sdk/packages`, then runs `make mmwDemo` (the MSS-only metaimage).

Outputs in `build/`: `iwr1843_sar_lvds.bin` (flash image), `iwr1843_sar_lvds.elf` (MSS `.xer4f`),
`iwr1843_sar_lvds_mss.map`, `build_info.txt`, `compilers.txt`.

## Build, flash, run

From `firmware_dev/` (see `projects/README.md` for prerequisites):

```bash
./fw build iwr1843_sar_lvds                 # outputs in projects/iwr1843_sar_lvds/build/
./fw flash iwr1843_sar_lvds /dev/ttyACM0    # prints the manual steps, exits 3
```

`flash.sh` has no headless flasher to call (no xWR18xx serial-flash target in the image's
DSLite; TI's tool is the UniFlash GUI), so it prints the steps: IWR1843BOOST SOP0+SOP2 closed
(flashing mode), power on, UniFlash with the CLI/UART port and `build/iwr1843_sar_lvds.bin` as
Meta Image 1, then SOP0 only (functional mode) and power-cycle. Send a cfg over the CLI port at
115200 baud. The TI profiles in `configs/` are references only: they contain object-detection commands
this firmware rejects. To reconfigure, send `sensorStop`, `flushCfg`, the full cfg and `sensorStart`;
no power cycle is needed unless `channelCfg`, `adcCfg` or `lowPower` change.

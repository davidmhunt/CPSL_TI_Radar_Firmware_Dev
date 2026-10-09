# Changes vs TI

Baseline: `project.toml` (`[source]` `baseline`, `baseline_commit`). See every change since with
`git diff <BASELINE_COMMIT> -- projects/iwr1843_sar_lvds/src`. Changes are listed oldest first.

The baseline was copied verbatim from `packages/ti/demo/xwr18xx/mmw/` of the SDK: `mss/ dss/ include/ makefile
mmw_res.h` into `src/`, `profiles/*.cfg` and `profiles/mmwDemo_xwr18xx_update_config.pl` into `configs/` and `tools/`
(both later removed as byte-identical duplicates of `ti_stock_demos/configs/xwr18xx/`). Left out (not source):
`docs/` (generated doxygen) and TI's prebuilt outputs (`*.bin *.map *.xer4f *.xe674 *.rov.xs`, including the AOP variants).

## 1. DSP chain removed, MSS-only, restartable, periodic calibration off

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
    `aoaFovCfg`, `cfarFovCfg`, `extendedMaxVelocity`, `configDataPort`. A stock TI cfg (such as
    the profiles in `ti_stock_demos/configs/xwr18xx/`) now fails on the first of these.
  - build: `makefile` builds `mmwDemo` = `mssDemo` + `generateMetaImage.sh <bin> $(SHMEM_ALLOC) <mss> <radarss> NULL`
    (`SHMEM_ALLOC` = SDK default `0x00000008`); DSS, AOP and secure (HS) targets dropped; `mmw_mss.mak` no
    longer links `libdpm`; the linker cmd no longer places `.demoSharedMem` in HS_RAM.
- **Kept**: the SDK mmWave CLI extension (channel/adc/profile/chirp/frame cfg, `flushCfg`), `adcbufCfg`
  (complex, chirpThreshold 1), `lvdsStreamCfg` (HW session, dataFmt 0/1/4, header on/off, dataFmt 2 since the metadata
  change below; `enableSW 1` rejected), `analogMonitor`, `CQRxSatMonitor`, `CQSigImgMonitor`, `calibData`,
  `queryDemoStatus`, `sensorStart`, `sensorStop`; calibration save/restore to flash; HSI clock 600 Mbps DDR; CLI prompt
  `mm_sar_lvds:/>` (banner names this firmware).
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
    sample count and the sample rate. `sensorStart 0` restarts with the previous cfg. A run with a finite
    `numFrames` that has finished still needs `sensorStop` before the next `sensorStart` (otherwise "Ignored:
    Sensor is already started"). `channelCfg`, `adcCfg` and `lowPower` are applied by `MMWave_open` on the first
    start only (stock behaviour); a later start with different values is rejected with a CLI error (the TI demo
    halted the MSS instead). Changing them needs a power cycle. After `flushCfg` they must be re-sent with the
    original values. A full cfg means the mmWave commands plus `adcbufCfg`, `lvdsStreamCfg`, `analogMonitor` and
    `calibData` (stock rule: all four must be re-sent after a stop for `sensorStart` to reconfigure).
  - **Periodic runtime calibration off**: `MMWave_start` gets `enablePeriodicity = false` (TI: every 10
    frames). Boot calibration (first `MMWave_open`) and TI's one-time runtime calibration at each
    `sensorStart` stay on.
  - Errors in the start path are reported on the CLI and undo the partial setup instead of asserting.

## 2. Per-chirp SAR metadata over LVDS (`lvdsStreamCfg` dataFmt 2) and `sarStats`

Format and host parsing: [`lvds_data_format.md`](lvds_data_format.md).

- **Added** `mss/mmw_sar_meta.{c,h}`: the 32-byte record (magic, version, flags, frame index, chirp in frame, chirps
  per frame, global chirp index, run index, saturation of an earlier chirp with its lag, 64-bit timestamp from the
  BIOS RTI counter at 100 MHz); two record slots in L3 (`.cbuffL3Memory`, placed by `mss/mmw_mss_linker.cmd`); three
  interrupt handlers: chirp start (VIM 99, own Hwi) fills chirp k's record into slot k mod 2, chirp available (VIM 123,
  SOC listener) counts the saturated CQ2 primary slices, frame start (VIM 98, SOC listener) cross-checks the counters.
  A 1 s BIOS clock function keeps the 64-bit timestamp extension current.
- **Changed**: `lvdsStreamCfg` accepts dataFmt 2 = CBUFF `ADC_USER` with the slots as its user buffer
  (`mss/mmw_lvds_stream.c`); `sensorStart` rejects dataFmt 2 unless the ADC output is complex, ChanInterleave is 1 and
  numAdcSamples x RX is even. `mss/mmw_mss.mak` compiles the SDK's `drivers/cbuff/platform/cbuff_xwr18xx.c` with
  `ENABLE_ALL_NON_INTERLEAVED`, because TI's prebuilt CBUFF library leaves `ADC_USER` out. dataFmt 1 and 4 stream as
  before; the interrupt handlers run in every format (they feed `sarStats`).
- **New CLI** `sarStats` (no arguments, works while running, reset at every `sensorStart`): chirps, frames, chirp-start
  and chirp-available interrupts, saturated chirps, late and missed chirp interrupts, counter resyncs, CBUFF error
  interrupts, the sticky CBUFF chirp/frame-start error bits, LVDS frames done, current timestamp.

## Build facts

`./fw build iwr1843_sar_lvds` exits 0 with no compiler or linker warnings (warnings are errors in the SDK flags).
`.bin` 151940 B with the metadata change (147844 B before it; TI baseline image 324804 B); the map takes the CBUFF
format table from the project's `cbuff_xwr18xx.oer4f` and places the record slots at L3 `0x51000000` (64 B); no
`objdet`/`DPM_`/`DPC_` symbol in the MSS map; the metaimage is generated with DSS `NULL` and no `.xe674` is built.
Baseline equivalence: the unmodified `src/` built to the same 324804 B `.bin` as `ti_stock_demos`, byte-identical at a
path of the same length.

## License of the TI sources

The TI sources in `src/`, `configs/` and `tools/` are tracked under TI's own terms. The C sources and
headers carry TI's BSD-3-clause header ("Redistribution and use in source and binary forms ..."). Files without
that header fall into two groups:

- **No header at all:** `makefile`, `*.mak`, `*_linker.cmd`, the TI `*.cfg` chirp profiles and the `.pl` helper (now only in `ti_stock_demos`).
- **Old header:** the XDC/BIOS config `mss/mmw_mss.cfg` carries an older "Copyright 2011 ... Restricted rights"
  boilerplate instead of the BSD text.

Both groups are covered by the SDK software manifest (`docs/mmwave_sdk_software_manifest.html`: "mmwave drivers,
control, datapath, utils & demo", `packages\ti\demo`, BSD-3-Clause). Keep these headers when editing. No TI binaries
are tracked; the open release-gate item on shipping TI binaries publicly is unchanged by this project.

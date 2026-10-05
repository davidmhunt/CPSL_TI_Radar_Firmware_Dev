# LVDS streaming code map (iwr1843_sar_lvds)

Where the MSS-only firmware in `src/` streams raw ADC data over LVDS to the DCA1000, and where the per-chirp metadata
record (dataFmt 2, firmware-08; format in `lvds_data_format.md`) is filled and streamed. Terms (CBUFF, HSI header, HW/SW session, MSS) as defined in `sar_feasibility.md`; EDMA moves the data. Paths are relative to `projects/iwr1843_sar_lvds/src/`; line numbers were
re-checked after firmware-08 (firmware-07 removed the DSP chain, SW session and TLV output; the unmodified TI source is `BASELINE_COMMIT`
in `project.env`). SDK paths are relative to `/opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti` (build container).

## `mss/mmw_lvds_stream.c`

| Lines | What |
|---|---|
| :78 | `MmwDemo_LVDSStream_EDMAInit` (HW session EDMA table only) |
| :119 | `MmwDemo_LVDSStreamInit`: lanes, DDR clock, MSB-first :140-144; `CBUFF_init` :147 (1 session); `HSIHeader_init` :156 |
| :320 | `MmwDemo_LVDSStreamDeleteHwSession`: deletes session + HSI header, fails if any EDMA channel was not returned |
| :374 | HW frame-done callback: counts frames only (session stays active, nothing waits) |
| :394 | `MmwDemo_LVDSStreamHwConfig`: `HSIHeader_createHeader` :450, `CBUFF_createSession` :465 |
| :420-443 | HW data format selection; dataFmt 2 = `CBUFF_DataFmt_ADC_USER` with the record slots as user buffer :425-432 |
| :503 | `MmwDemo_configLVDSHwData`: (re)creates and activates the HW session |

## `mss/mmw_sar_meta.{c,h}` (per-chirp metadata, firmware-08)

| Lines | What |
|---|---|
| `.h`:51-77 | `MmwDemo_SarChirpMeta` record + size/offset static asserts |
| :68 | RTI FRC0 timestamp source |
| :147 | record slots in `.cbuffL3Memory` (L3, placed by `mss/mmw_mss_linker.cmd`) |
| :182 | chirp-start handler (VIM 99): fills slot `globalChirpIdx & 1`, LATE/SKIP detection |
| :290 | frame-start listener (VIM 98): counter cross-check / resync |
| :346 | chirp-available listener (VIM 123): CQ2 saturation count |
| :416, :485, :510, :556 | init (Hwi, listeners, 1 s clock), per-reconfig constants, per-run reset, `sarStats` print |

## `mss/mss_main.c`

| Lines | What |
|---|---|
| :78-93 | LVDS HW data sizing notes (TI's demo notes, header padding corrected to 16 B) |
| :1730, :1737 | `MmwDemo_LVDSStreamInit` and `MmwDemo_sarMetaInit` at startup |
| :840, :1030 | `MmwDemo_mssSetHsiClk` (600 Mbps DDR, :852), called at first sensor start from `MmwDemo_openSensor` |
| :609 | `MmwDemo_dataPathStart`: ADCBUF open + config, CQ, HW session created/activated (:659), every sensor start |
| :683 | `MmwDemo_dataPathStop`: HW session deactivate + delete (:694), `ADCBuf_close` (:707) |
| :785-793 | BSS frame-end event posts `frameEndSemHandle` |
| :1117-1149 | `MmwDemo_configSensor`: dataFmt 2 checks, `MmwDemo_sarMetaConfig` |
| :1182 | `MmwDemo_startSensor`: `MmwDemo_sarMetaRunStart` before `MMWave_start` |
| :1284, :1291 | `MmwDemo_stopSensor`: bounded wait for frame end, then teardown |
| :1194-1195 | runtime calibration at start: one-time on, periodic off |
| :1794 | mmWave initialized in isolation mode (MSS-only) |

## Headers, CLI, resources

- `mss/mmw_lvds_stream.h:62-99`: `MmwDemo_LVDSStream_MCB_t` (HW session only).
- `include/mmw_config.h:59-92`: `MmwDemo_LvdsStreamCfg` (format defines :68-77, dataFmt 2 :74).
- `mss/mmw_cli.c:595`: `lvdsStreamCfg <subFrameIdx> <enableHeader> <dataFmt> <enableSW>` (enableSW 1 rejected :622, dataFmt whitelist :626-632), registered :794; `sarStats` :677, registered :804.
- `mss/mmw_mss.mak`: builds `mmw_sar_meta.c` and the SDK's `drivers/cbuff/platform/cbuff_xwr18xx.c` with `ENABLE_ALL_NON_INTERLEAVED` (enables `ADC_USER`).
- `mmw_res.h:53-95`: EDMA channels for the CBUFF trigger and the HW session.
- SDK: `utils/hsiheader/hsiprotocol.h` `HSIDataCardHeader` :403, `HSISDKHeader` :441, `HSIHeader` :590.

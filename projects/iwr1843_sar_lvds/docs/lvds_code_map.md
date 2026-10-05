# LVDS streaming code map (iwr1843_sar_lvds)

Where the MSS-only firmware in `src/` streams raw ADC data over LVDS to the DCA1000, and where a per-chirp record
would hook in. Terms (CBUFF, HSI header, HW/SW session, MSS) as defined in `sar_feasibility.md`; EDMA moves the data. Paths are relative to `projects/iwr1843_sar_lvds/src/`; line numbers were
re-checked after firmware-07 (DSP chain, SW session and TLV output removed; the unmodified TI source is `BASELINE_COMMIT`
in `project.env`). SDK paths are relative to `/opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti` (build container).

## `mss/mmw_lvds_stream.c`

| Lines | What |
|---|---|
| :77 | `MmwDemo_LVDSStream_EDMAInit` (HW session EDMA table only) |
| :118 | `MmwDemo_LVDSStreamInit`: lanes, DDR clock, MSB-first :139-142; `CBUFF_init` :146 (1 session); `HSIHeader_init` :155 |
| :319 | `MmwDemo_LVDSStreamDeleteHwSession`: deletes session + HSI header, fails if any EDMA channel was not returned |
| :373 | HW frame-done callback: counts frames only (session stays active, nothing waits) |
| :392 | `MmwDemo_LVDSStreamHwConfig`: `HSIHeader_createHeader` :439, `CBUFF_createSession` :454 |
| :418-433 | HW data format selection: the per-chirp `CBUFF_DataFmt_ADC_USER` hook (see `sar_feasibility.md` (d)) |
| :492 | `MmwDemo_configLVDSHwData`: (re)creates and activates the HW session |

## `mss/mss_main.c`

| Lines | What |
|---|---|
| :73-91 | LVDS HW data sizing notes (from TI's demo notes): HSI header sizing, 64 B minimum transfer, 0x3FFF-unit link-list limit |
| :1673 | `MmwDemo_LVDSStreamInit` at startup |
| :830, :842 | `MmwDemo_mssSetHsiClk` (600 Mbps DDR), called at first sensor start from `MmwDemo_openSensor` |
| :599 | `MmwDemo_dataPathStart`: ADCBUF open + config, CQ, HW session created/activated (:649), every sensor start |
| :673 | `MmwDemo_dataPathStop`: HW session deactivate + delete (:684), `ADCBuf_close` (:697) |
| :775-783 | BSS frame-end event posts `frameEndSemHandle` |
| :1227, :1234 | `MmwDemo_stopSensor`: bounded wait for frame end, then teardown |
| :1137-1138 | runtime calibration at start: one-time on, periodic off |
| :1730 | mmWave initialized in isolation mode (MSS-only) |

## Headers, CLI, resources

- `mss/mmw_lvds_stream.h:62-99`: `MmwDemo_LVDSStream_MCB_t` (HW session only).
- `include/mmw_config.h:59-89`: `MmwDemo_LvdsStreamCfg` (format defines :68-74).
- `mss/mmw_cli.c:593`: `lvdsStreamCfg <subFrameIdx> <enableHeader> <dataFmt> <enableSW>` (enableSW 1 rejected :620), registered :775.
- `mmw_res.h:53-95`: EDMA channels for the CBUFF trigger and the HW session.
- SDK: `utils/hsiheader/hsiprotocol.h` `HSIDataCardHeader` :403, `HSISDKHeader` :441, `HSIHeader` :590.

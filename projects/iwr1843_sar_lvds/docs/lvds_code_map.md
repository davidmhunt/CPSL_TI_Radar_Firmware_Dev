# LVDS streaming code map (iwr1843_sar_lvds)

Where the TI mmw demo in `src/` streams raw ADC data over LVDS to the DCA1000, and where a per-chirp record
would hook in. Terms: CBUFF is the SDK's LVDS streaming driver; the HSI header is the per-transfer header the DCA1000
expects; a HW session streams ADC data from the ADC buffer, a SW session streams CPU buffers; MSS is the Cortex-R4F
master subsystem; EDMA moves the data. Paths are relative to `projects/iwr1843_sar_lvds/src/`; line numbers were
re-checked at `src/` HEAD (`BASELINE_COMMIT` in `project.env` is unmodified TI source). SDK paths are relative to
`/opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti` (build container).

## `mss/mmw_lvds_stream.c`

| Lines | What |
|---|---|
| :80 | `MmwDemo_LVDSStream_EDMAInit` |
| :130 | `MmwDemo_LVDSStreamInit`: `CBUFF_init` :159; lanes, DDR clock, MSB-first :152-156; `HSIHeader_init` :177 |
| :630 | `MmwDemo_LVDSStreamHwConfig`: `HSIHeader_createHeader` :678, `CBUFF_createSession` :693 |
| :656-662 | HW data format selection: the per-chirp `CBUFF_DataFmt_ADC_USER` hook (see `sar_feasibility.md` (d)) |
| :724 | `MmwDemo_LVDSStreamSwConfig`: per-frame user buffers :742-752 (the demo's header and point cloud); fallback site only, see `sar_feasibility.md` (d) |
| :794 | `MmwDemo_configLVDSHwData` (deletes the old HW session, then HwConfig) |

## `mss/mss_main.c`

| Lines | What |
|---|---|
| :331-450 | TI's design notes on LVDS streaming: HSI header sizing (:366-379), 64 B minimum transfer, 0x3FFF-unit link-list limit (:443), EDMA split |
| :3737 | `MmwDemo_LVDSStreamInit` at startup |
| :2900, :3090 | `MmwDemo_mssSetHsiClk` (600 Mbps DDR, :2911-2912) and its call at first sensor start |
| :1986, :2840 | HW session configured per sub-frame (first frame, then next sub-frame) |
| :2618-2642 | per-frame SW session: delete, `SwConfig`, fill header, activate |
| :2736-2778 | HW and SW frame-done semaphore waits |
| :3268-3301 | teardown: deactivate and delete sessions |
| :3164-3166 | stock periodic calibration (every 10 frames) passed to `MMWave_start` |
| :3814 | mmWave initialized in isolation mode (relevant to an MSS-only image) |

## Headers, CLI, resources

- `mss/mmw_lvds_stream.h:68-87`: `MmwDemo_LVDSUserDataHeader_t` (frameNum, subFrameNum, detObjNum).
- `include/mmw_config.h:95-125`: `MmwDemo_LvdsStreamCfg` (format defines :104-110).
- `mss/mmw_cli.c:1098`: `lvdsStreamCfg <subFrameIdx> <enableHeader> <dataFmt> <enableSW>`, registered :1420.
- `mmw_res.h:243-262`: EDMA channels for the CBUFF, HW and SW sessions.
- SDK: `utils/hsiheader/hsiprotocol.h` `HSIDataCardHeader` :403, `HSISDKHeader` :441, `HSIHeader` :590.

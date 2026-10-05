# LVDS streaming code map (iwr1843_sar_lvds)

Where the TI mmw demo in `src/` streams raw ADC data over LVDS to the DCA1000, and where a custom
per-chirp payload would hook in. Line numbers are against `src/` at the pristine TI baseline
(`BASELINE_COMMIT` in `project.env`); `src/` paths below are relative to `projects/iwr1843_sar_lvds/src/`.
SDK paths are relative to `/opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti` (inside the build container).

## Design notes

- `mss/mss_main.c:331-450`: TI's notes on LVDS streaming: HSI header sizing, 64 B minimum transfer,
  the 0x3FFF-unit link-list limit (:443), EDMA split.

## `mss/mmw_lvds_stream.c`

| Lines | What |
|---|---|
| :80 | `MmwDemo_LVDSStream_EDMAInit` |
| :130 | `MmwDemo_LVDSStreamInit`: `CBUFF_init` :159; lanes, DDR clock, MSB-first :152-156; `HSIHeader_init` :177 |
| :630 | `MmwDemo_LVDSStreamHwConfig`: data format :656-662, `HSIHeader_createHeader` :678, `CBUFF_createSession` :693 |
| :724 | `MmwDemo_LVDSStreamSwConfig`: user buffers :742-752 (a custom SAR payload goes here) |
| :794 | `MmwDemo_configLVDSHwData` (deletes the old HW session, then HwConfig) |
| :529, :579 | HW / SW frame-done callbacks |
| :450, :491 | HW / SW session delete |

## `mss/mss_main.c` call sites

| Lines | What |
|---|---|
| :3737 | `MmwDemo_LVDSStreamInit` at startup |
| :2900, :3090 | `MmwDemo_mssSetHsiClk` (600 Mbps DDR, :2911-2912) and its call at first sensor start |
| :1986, :2840 | HW session configured per sub-frame (first frame, then next sub-frame) |
| :2618-2642 | per-frame SW session: delete, `SwConfig`, fill header, activate |
| :2736-2778 | HW and SW frame-done semaphore waits |
| :3268-3301 | teardown: deactivate and delete sessions |
| :3164-3166 | stock periodic calibration (every 10 frames) passed to `MMWave_start` |

## Headers, CLI, resources

- `mss/mmw_lvds_stream.h:68-87`: `MmwDemo_LVDSUserDataHeader_t` (frameNum, subFrameNum, detObjNum).
- `include/mmw_config.h:95-125`: `MmwDemo_LvdsStreamCfg` (format defines :104-110).
- `mss/mmw_cli.c:1098`: `lvdsStreamCfg <subFrameIdx> <enableHeader> <dataFmt> <enableSW>`, registered :1420.
- `mmw_res.h:243-262`: EDMA channels for the CBUFF, HW and SW sessions.
- SDK: `utils/hsiheader/hsiprotocol.h` `HSIDataCardHeader` :403, `HSISDKHeader` :441, `HSIHeader` :590;
  `utils/hsiheader/src/hsiheader.c:710` `HSIHeader_createHeader`;
  `drivers/cbuff/docs/CBUFF_Transfers.pptx` (CBUFF transfer diagrams).

## Bench check

Flash and send a cfg per the project README ("Build, flash, run"). The 18xx data port runs at 921600
baud; a cfg is accepted once per power-up.

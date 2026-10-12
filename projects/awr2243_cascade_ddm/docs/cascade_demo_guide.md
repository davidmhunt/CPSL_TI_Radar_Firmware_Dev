# Cascade DDM demo: developer guide

For firmware and host-driver developers: how the image is built, what is in `src/`, and the UART packet format in
enough detail to write a parser. Paths are relative to `projects/awr2243_cascade_ddm/` unless they start with `mmw/`
(`src/ti/demo/am273x/mmw/`); a cite `file:N` is a line in this tree, read from source. The older manual `make` build and
the raw-ADC notes are in [`archive/`](archive/) and are not verified. Terms: [glossary](../../../docs/reference/fw_cli.md#glossary).

## 1. What runs where

| Part | Runs on | Source |
|------|---------|--------|
| MSS | AM273x Cortex-R5F, little-endian (`src/mmwave2chipCascade_mss.projectspec:45`) | `mmw/mss/`: `mss_main.c` (control, UART output), `mmw_cli.c` (CLI), `mss_tracker.c` (GTrack) |
| DSS | AM273x C66x DSP | `mmw/dss/`: `dss_main.c`, `data_path.c` (range/Doppler/CFAR datapath) |
| BSS | each AWR2243 | not in this tree; the MSS configures both chips over mmWaveLink (`src/ti/control/mmwavelink/`) |

The host sends ASCII CLI lines on the command UART and reads frames from the data UART. The command UART is
`CONFIG_UART0` at 115200 baud (`mmw/mss/mss_main.c:4249`, `:4469`). The data UART is `CONFIG_UART1`
(`mss_main.c:4477`) at 3,125,000 baud (`mmw/mss/mss.syscfg:138`; `configDataPort <baud>` changes it at
run time, max 3,125,000, `mmw_cli.c:156`, `:1689`).

## 2. Build

`./fw build awr2243_cascade_ddm` (from `firmware_dev/`, about 15 minutes) runs `build.sh` in the
container. It does not use the SDK's `make` flow. In order:

1. Copies `src/` to `build/stage/` and removes `build/ccs_workspace/` (`build.sh:60-64`).
2. Adds seven prebuilt TI libraries (`build.sh:41-49`) to the stage from the Radar Toolbox (`radar_toolbox_4_00_00_05`)
   unless a copy exists under `src/ti/**/lib/` (`build.sh:66-78`): DPM (two files, `.ae66` for the DSS and `.aer5f`
   for the MSS), mmWave, mmWaveLink and CLI (`.aer5f`), DDMA range and Doppler (`.ae66`). They are not tracked here.
3. Patches the staged `.projectspec` files for Linux: CCS's cygwin `rm` becomes `rm`, `node.exe`
   becomes `node` (`build.sh:82-85`). `src/` is not changed.
4. Registers the TI install dirs with CCS and imports both projectspecs headlessly
   (`build.sh:87-95`).
5. Builds `am273x_mmw_cascade_demo_dss` (C66x, `.xe66`), then `am273x_mmw_cascade_demo_mss` (R5F,
   `.xer5f`) in configuration `Release`, or `Debug` with `CCS_CONFIG=Debug` (`build.sh:97-110`). It
   fails if either output is missing, because CCS does not always return non-zero on compile errors.
6. The MSS post-build step strips each ELF, converts it to `.rprc` and joins them with
   `multicoreImageGen.js --devID 55`: MSS at core slot 0, DSS at 2, into
   `am273x_mmw_cascade_demo_DDM.appimage` (`src/mmwave2chipCascade_mss.projectspec:88-90`). An appimage is the one file
   the SBL (secondary bootloader) loads; this one holds the two AM273x programs only, no AWR2243 BSS patch.
7. Copies the outputs to `build/` as `am273x_cascade.appimage` (flashed), `am273x_cascade.elf` (MSS),
   `am273x_cascade_dss.xe66`, and writes `build_info.txt`; `fw build` adds `build_info.json`
   (`build.sh:112-129`).

Compiler options come from the projectspecs: `-DMMWDEMO_DDM -DCASCADE_EVM` (MSS, `:54-55`) and
`--define=MMWDEMO_DDM --define=CASCADE_EVM` (DSS, `src/mmwave2chipCascade_dss.projectspec:62-63`).

## 3. Source tree (`ls src/`)

```text
src/
  mmwave2chipCascade_{mss,dss}.projectspec   CCS projects (read by build.sh)
  ti/
    alg/gtrack/            GTrack tracker (3D build)
    board/antenna_geometry.h
    common/syscommon.h
    control/mmwave/, control/mmwavelink/   mmWave control and SPI link to the AWR2243s
    datapath/dpc/objectdetection/{common,objdethwa,objdethwaDDMA}/   detection chains (TDM and DDMA)
    datapath/dpu/{rangeprocDDMA,dopplerprocDDMA}/
    demo/am273x/mmw/       mss/, dss/, include/ (mmw_config.h, mmw_output.h), mmw_resDDM.h, mmw_resTDM.h
    demo/io_interface/, demo/utils/   shared demo helpers (RF parser, ADC config, flash, Ethernet)
    utils/hsiheader/
```

The DPM and CLI libraries have no source here; they come as the prebuilt libraries in step 2.

## 4. DDM, not TDM

The tree carries both TI chains, but this project builds only DDM: both projectspecs define
`MMWDEMO_DDM` and none defines `MMWDEMO_TDM` (lines in section 2). `mmw_output.h:47-51` then includes the
`objdethwaDDMA` chain and `mmw_resDDM.h` supplies the resources. In DDM all enabled transmitters fire in
every chirp and are separated in the Doppler FFT, which is divided into `numTxAnt + numEmptySubBands`
sub-bins (`mmw/mss/mss_main.c:1614`, `:3823`); TDM fires them in turn. The shipped cfgs enable TX mask 7 on every chirp (`chirpCfg 0 7 0 0 0 0 0 7`, `configs/cascade_shortrange.cfg:18`).

## 5. CLI and the shipped cfgs

`configs/cascade_*.cfg` are CLI scripts: one command per line, `%` starts a comment. Each line answers
`Done` or an error. The demo-specific commands are the table at `mmw_cli.c:2153-2403` (for example
`sensorStart`, `sensorStop`, `guiMonitor`, `cfarCfg`, `aoaFovCfg`, `gtrack`, `ransac`,
`configDataPort`, `queryDemoStatus`); the chirp commands (`profileCfg`, `chirpCfg`, `frameCfg` and so
on) are not in this table. A cfg is accepted once per power-up.

`guiMonitor <subFrameIdx> <detectedObjects> <logMagRange> <noiseProfile> <rangeAzimuthHeatMap> <rangeDopplerHeatMap>
<statsInfo> <ransacFilterMask> <trackers>` (`mmw_cli.c:2246-2250`, parsed at `:490-498`; flags in
`mmw/include/mmw_config.h:67-95`) selects the TLVs. `detectedObjects`: 1 = points + side info, 2 = points only,
3 = compact points, 0 = none. `cascade_shortrange.cfg`, `cascade_longrange.cfg` and `cascade_verylongrange.cfg` use
`guiMonitor -1 3 0 0 0 0 0 1 1` (line 20): compact points (type 12), RANSAC mask (11), tracker (10); no type 1, 7 or 6.
`cascade_shortrange_calib.cfg` uses `-1 3 1 1 0 0 0 0 0` (line 16): types 12, 2 and 3. (`-1` as the sub-frame: its
meaning was not checked.)

## 6. UART frame format (data port)

All fields are little-endian. One frame is `header`, then `numTLVs` TLVs (type-length-value records), then zero padding up to a
multiple of 32 bytes (`MMWDEMO_OUTPUT_MSG_SEGMENT_LEN`, `mmw/include/mmw_output.h:57`; no padding if already a multiple,
`mss_main.c:1951-1958`). There is one frame per sub-frame per chirp frame, back to back with no gap. Resync: search the
stream for the 8 magic bytes, read `totalPacketLen` at +12 and accept the frame only if the next 8 bytes after that
length are the magic again (or the stream ends). A corrupt `totalPacketLen` is not otherwise guarded by the format.

### 6.1 Header, 40 bytes (`MmwDemo_output_message_header`, `mmw_output.h:119-149`)

| Offset | Field | Type | Value, source |
|--------|-------|------|---------------|
| 0-7 | `magicWord[4]` | 4 x uint16 (8 bytes) | `0x0102,0x0304,0x0506,0x0708`, so the bytes are `02 01 04 03 06 05 08 07` (`mss_main.c:1651-1654`) |
| 8 | `version` | uint32 | `major<<24 \| minor<<16 \| bugfix<<8 \| build` of the mmWave SDK (`mss_main.c:1656-1659`); the SDK header in the build workspace (`ti/common/mmwavesdk_version.h:48-51`) gives 4.4.0.1, so `0x04040001`; not read off a live frame |
| 12 | `totalPacketLen` | uint32 | header + TLVs + padding, rounded up to 32 (`mss_main.c:1747-1748`) |
| 16 | `platform` | uint32 | always `0x2243` (`mss_main.c:1650`) |
| 20 | `frameNumber` | uint32 | `frameStartIntCounter` from the DSS stats (`mss_main.c:1750`) |
| 24 | `timeCpuCycles` | uint32 | always 0 (`mss_main.c:1749`, the counter read is commented out) |
| 28 | `numDetectedObj` | uint32 | points in this frame, `gNumObjOut` (`mss_main.c:1655`) |
| 32 | `numTLVs` | uint32 | TLVs that follow (`mss_main.c:1745`) |
| 36 | `subFrameNumber` | uint32 | 0 without advanced frame config, else 0 to sub-frames-1 (`mmw_output.h:144-148`, `mss_main.c:1751`) |

### 6.2 TLV header, 8 bytes (`MmwDemo_output_message_tl`, `mmw_output.h:210-218`)

`uint32 type` then `uint32 length`; `length` is the payload size in bytes and does not include these 8
bytes. The payload follows at once. TLVs are not padded between each other; only the frame end is.

### 6.3 TLV types

Type numbers are the enum at `mmw_output.h:67-110`, counted from 1. A TLV is sent only if its `guiMonitor`
flag is set and, for point lists, `numDetectedObj > 0`. Lengths: `mss_main.c:1661-1743`; sending order (the
stream order): `mss_main.c:1760-1950`.

| Order | Type | Name | Payload length | Sent when |
|-------|------|------|----------------|-----------|
| 1 | 1 | `DETECTED_POINTS` | 16 x N | `detectedObjects` 1 or 2 |
| 2 | 12 | `DETECTED_POINTS_COMPACT` | 8 x N | `detectedObjects` 3 |
| 3 | 7 | `DETECTED_POINTS_SIDE_INFO` | 4 x N | `detectedObjects` 1 |
| 4 | 2 | `RANGE_PROFILE` | 2 x `numRangeBins` | `logMagRange` |
| 5 | 3 | `NOISE_PROFILE` | 2 x `numRangeBins` | `noiseProfile` |
| 6 | 5 | `RANGE_DOPPLER_HEAT_MAP` | 2 x `numRangeBins` x `numDopFFTSubBins` (DDM) | `rangeDopplerHeatMap` |
| 7 | 6 | `STATS` | 24 | `statsInfo` (also sends type 9) |
| 8 | 9 | `TEMPERATURE_STATS` | 28 | `statsInfo` |
| 9 | 10 | `TRACKER` | 40 x T | `trackers` and T > 0 |
| 10 | 11 | `RANSAC_FILTER_MASK` | 1 x N | `ransacFilterMask` and `ransac` enabled |

N = `numDetectedObj`, T = tracked targets (independent of N: the tracker TLV can be sent when N = 0, `mss_main.c:1732`).
When N = 0 the point TLVs (1, 12, 7) are absent, so `numTLVs` is smaller; the RANSAC mask has N bytes. Types 4 and 8 (TDM
heat map, unused) and 13 (`OUTPUT_SNR`) are never sent by this build. `numRangeBins` and `numDopFFTSubBins` are not on
the wire: they come from the cfg (`mss_main.c:2406`, `:1614`).

### 6.4 Payload layouts

Point (type 1), 16 bytes, repeated N times: `float x, y, z, velocity` (metres; m/s). +x is to the right of the
sensor, +y away from it, +z up; positive velocity is moving away. Source: SDK
`ti/datapath/dpif/dpif_pointcloud.h:58-79` (`DPIF_PointCloudCartesian`; this file is in the TI SDK, not in
`src/`; the sizes also follow from `mss_main.c:1665`). Axis signs are as the SDK header states; not checked on the bench.

Side info (type 7), 4 bytes per point in the same order: `int16 snr, int16 noise`, units of 0.1 dB
(`dpif_pointcloud.h:95-102`).

Compact point (type 12), 8 bytes per point, N times: `int16 azimSinPhaseQuantized, int16 elevSinPhaseQuantized,
int16 rangeIdx, int16 dopplerIdx` (`src/ti/datapath/dpc/objectdetection/objdethwaDDMA/objectdetection.h:176-189`). To get
the type 1 values (`objectdetection.c:1185-1255`, same file under `.../objdethwaDDMA/src/`):

- `range = rangeIdx x rangeStep` (m); `velocity = dopplerIdx x dopplerStep` (m/s; `dopplerIdx` is already signed).
- `sinEl = elevSinPhaseQuantized / 32767`, `sinAz = azimSinPhaseQuantized / 32767` (`:1259-1260`).
- `z = range x sinEl`; `x = range x sqrt(1 - sinEl^2) x sinAz`; `y = sqrt(range^2 - x^2 - z^2)` (`:1201`, `:1222`, `:1234-1235`).
- `rangeStep` and `dopplerStep` are not sent. The firmware gets them from the chirp cfg (`mss_main.c:2502`, `:2532`), so the host
  must derive them from the same cfg; the formula was not checked here. With `detectedObjects` 1 the firmware sends metres directly.

The C++ driver knows type 12 but skips its payload (`CPSL_TI_Radar_cpp/src/SerialStreamer/UartFrame.cpp:17`, `:185-186`).

Range and noise profiles (types 2, 3): `uint16` per range bin, `numRangeBins` entries, copied from the detection matrix `detMatrix`
(range profile: first Doppler sub-bin of each range row, `mss_main.c:1818-1838`; noise profile: `detMatrix[index*numDopplerBins +
numDopplerBins/2-1]`, `:1840-1857`). Units and exact meaning are not verified. Range-Doppler map (type 5): `detMatrix` as
`uint16`, range-major, `numRangeBins x numDopFFTSubBins` (`mss_main.c:1877-1890`); not verified on the bench.

Stats (type 6), 24 bytes, six `uint32`: `interFrameProcessingTime`, `transmitOutputTime` (us),
`interFrameProcessingMargin`, `interChirpProcessingMargin` (us), `activeFrameCPULoad`,
`interFrameCPULoad` (%) (`mmw_output.h:159-178`).

Temperature (type 9), 28 bytes (`mmw/mss/mmw_mss.h:328-339`): `int32 tempReportValid` (0 = valid), then
`rlRfTempData_t` (24 bytes, `src/ti/control/mmwavelink/include/rl_sensor.h:1757-1805`): `uint32 time` (ms
since BSS power-up) and ten `int16` sensors in this order: `tmpRx0..3Sens`, `tmpTx0..2Sens`, `tmpPmSens`,
`tmpDig0Sens`, `tmpDig1Sens`, 1 LSB = 1 deg C.

Tracker (type 10), 40 bytes per target (`MmwDemo_tracker_out`, `mmw_output.h:227-231`): nine `float`
`posX, posY, posZ, velX, velY, velZ, accX, accY, accZ` (m, m/s, m/s^2; the 3D state vector,
`src/ti/alg/gtrack/include/gtrack_3d.h:111-131`, selected by `#define GTRACK_3D` at `mmw_output.h:53`),
then `uint32 state`, which is the GTrack `trackState` (`mmw/mss/mss_tracker.c:210`). There is no target ID in this struct;
the `state` values are not defined in this tree (not verified).

RANSAC mask (type 11): one `uint8` per detected point, in point order (`mss_main.c:1739-1741`); the meaning of the
values was not checked.

## 7. Raw ADC (LVDS) is not in this image

`mmw/dss/mmw_lvds_stream.c` and `.h` are set `excludeFromBuild="true"` in
`src/mmwave2chipCascade_dss.projectspec:148-149`, and `LVDS_STREAM` is not defined in either projectspec
or any source. The code behind it is therefore not compiled: the `numLvdsLanesCfg` command
(`mmw_cli.c:2328-2331`) is absent, and there is no `lvdsStreamCfg` command. Nothing in `mmw/` calls
`MmwDemo_LVDSStreamSwConfig` either. The cascade streams point clouds over UART only; a DCA1000 UDP
layout and a per-chirp Rx layout cannot be derived from this build. The earlier text is kept, unverified,
in [`archive/lvds_raw_stream_unverified.md`](archive/lvds_raw_stream_unverified.md).

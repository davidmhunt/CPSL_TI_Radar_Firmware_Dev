# Cascade DDM demo: developer guide

For firmware and host-driver developers. It covers how the image is built, what is in `src/`, and
the UART packet format in enough detail to write a parser. Paths below are relative to
`projects/awr2243_cascade_ddm/` unless they start with `mmw/`, which stands for
`src/ti/demo/am273x/mmw/`. A cite `file:N` is a line in this tree; everything was read from source,
not from TI documentation. The `.projectspec` files are in `src/`; the older manual `make` build
and the raw-ADC notes are in [`archive/`](archive/) and are not verified.

## 1. What runs where

| Part | Runs on | Source |
|------|---------|--------|
| MSS | AM273x Cortex-R5F, little-endian (`src/mmwave2chipCascade_mss.projectspec:45`) | `mmw/mss/`: `mss_main.c` (control, UART output), `mmw_cli.c` (CLI), `mss_tracker.c` (GTrack) |
| DSS | AM273x C66x DSP | `mmw/dss/`: `dss_main.c`, `data_path.c` (range/Doppler/CFAR datapath) |
| BSS | each AWR2243 | not in this tree; the MSS configures both chips over mmWaveLink (`src/ti/control/mmwavelink/`) |

The host sends ASCII CLI lines on the command UART and reads frames from the data UART. The command UART is
`CONFIG_UART0` at 115200 baud (`mmw/mss/mss_main.c:4249`, `:4469`). The data UART is `CONFIG_UART1`
(`mss_main.c:4477`) at 3,125,000 baud (`mmw/mss/mss.syscfg:138`; `configDataPort <baud>` changes it at
run time, max 3,125,000, `mmw_cli.c:156`, `:1689`). `queryDemoStatus` prints a different default
(892857, `mss_main.c:4248`); that field is only printed, not applied.

## 2. Build

`./fw build awr2243_cascade_ddm` (from `firmware_dev/`, about 15 minutes) runs `build.sh` in the
container. It does not use the SDK's `make` flow. In order:

1. Copies `src/` to `build/stage/` and removes `build/ccs_workspace/` (`build.sh:60-64`).
2. Adds the seven prebuilt TI libraries (DPM x2, mmWave, mmWaveLink, DDMA range and Doppler, CLI; list
   in `build.sh:41-49`) to the stage from the Radar Toolbox (`radar_toolbox_4_00_00_05`) unless a copy
   exists under `src/ti/**/lib/` (`build.sh:66-78`). They are not tracked here.
3. Patches the staged `.projectspec` files for Linux: CCS's cygwin `rm` becomes `rm`, `node.exe`
   becomes `node` (`build.sh:82-85`). `src/` is not changed.
4. Registers the TI install dirs with CCS and imports both projectspecs headlessly
   (`build.sh:87-95`).
5. Builds `am273x_mmw_cascade_demo_dss` (C66x, `.xe66`), then `am273x_mmw_cascade_demo_mss` (R5F,
   `.xer5f`) in configuration `Release`, or `Debug` with `CCS_CONFIG=Debug` (`build.sh:97-110`). It
   fails if either output is missing, because CCS does not always return non-zero on compile errors.
6. The MSS post-build step strips each ELF, converts it to `.rprc` and joins them with
   `multicoreImageGen.js --devID 55`: MSS at core slot 0, DSS at 2, into
   `am273x_mmw_cascade_demo_DDM.appimage` (`src/mmwave2chipCascade_mss.projectspec:88-90`). The image
   holds the two AM273x cores only; no AWR2243 BSS patch is in it.
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
sub-bins (`mmw/mss/mss_main.c:1614`, `:3823`); TDM fires them in turn. The shipped cfgs enable TX mask 7 on every chirp (`chirpCfg 0 7 0 0 0 0 0 7`, `configs/cascade_shortrange.cfg:18`). The TDM objects stay in the tree as part of TI's baseline and are not compiled.

## 5. CLI and the shipped cfgs

`configs/cascade_*.cfg` are CLI scripts: one command per line, `%` starts a comment. Each line answers
`Done` or an error. The demo-specific commands are the table at `mmw_cli.c:2153-2403` (for example
`sensorStart`, `sensorStop`, `guiMonitor`, `cfarCfg`, `aoaFovCfg`, `gtrack`, `ransac`,
`configDataPort`, `queryDemoStatus`); the chirp commands (`profileCfg`, `chirpCfg`, `frameCfg` and so
on) are are not in this table. A cfg is accepted once per power-up.

`guiMonitor <subFrame> <detectedObjects> <logMagRange> <noiseProfile> <rangeAzimuthHeatMap>
<rangeDopplerHeatMap> <statsInfo> <ransacFilterMask> <trackers>` (`mmw_cli.c:2246-2250`, parsed at
`:490-498`) selects the TLVs (flags in `mmw/include/mmw_config.h:67-95`). `detectedObjects`: 1 = points +
side info, 2 = points only, 3 = compact points, 0 = none. Three shipped cfgs use `guiMonitor -1 3 0 0 0 0 0 1 1` (line 20);
`cascade_shortrange_calib.cfg` uses `-1 3 1 1 0 0 0 0 0`, so the shipped frames carry compact points (type 12), the
tracker list (type 10) and the RANSAC mask (type 11), but no type 1, 7 or 6.

## 6. UART frame format (data port)

All fields are little-endian. One frame is `header`, then `numTLVs` TLVs, then zero padding up to a
multiple of 32 bytes (`MMWDEMO_OUTPUT_MSG_SEGMENT_LEN`, `mmw/include/mmw_output.h:57`). Frames follow
each other with no gap. To resync, search for the magic word.

### 6.1 Header, 40 bytes (`MmwDemo_output_message_header`, `mmw_output.h:119-149`)

| Offset | Field | Type | Value, source |
|--------|-------|------|---------------|
| 0 | `magicWord[4]` | uint16 x4 | `0x0102,0x0304,0x0506,0x0708`, so the bytes are `02 01 04 03 06 05 08 07` (`mss_main.c:1651-1654`) |
| 8 | `version` | uint32 | `major<<24 \| minor<<16 \| bugfix<<8 \| build` of the mmWave SDK (`mss_main.c:1656-1659`) |
| 12 | `totalPacketLen` | uint32 | header + TLVs + padding, rounded up to 32 (`mss_main.c:1747-1748`) |
| 16 | `platform` | uint32 | always `0x2243` (`mss_main.c:1650`) |
| 20 | `frameNumber` | uint32 | `frameStartIntCounter` from the DSS stats (`mss_main.c:1750`) |
| 24 | `timeCpuCycles` | uint32 | always 0 (`mss_main.c:1749`, the counter read is commented out) |
| 28 | `numDetectedObj` | uint32 | points in this frame, `gNumObjOut` (`mss_main.c:1655`) |
| 32 | `numTLVs` | uint32 | TLVs that follow (`mss_main.c:1745`) |
| 36 | `subFrameNumber` | uint32 | sub-frame index; 0 without advanced frame config (`mss_main.c:1751`) |

### 6.2 TLV header, 8 bytes (`MmwDemo_output_message_tl`, `mmw_output.h:210-218`)

`uint32 type` then `uint32 length`; `length` is the payload size in bytes and does not include these 8
bytes. The payload follows at once. TLVs are not padded between each other; only the frame end is.

### 6.3 TLV types

Type numbers are the enum at `mmw_output.h:67-110`, counted from 1. A TLV is sent only if its `guiMonitor`
flag is set and, for point lists, `numDetectedObj > 0`. Building the lengths: `mss_main.c:1661-1743`;
sending order (this is the order in the stream): `mss_main.c:1760-1950`.

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

N = `numDetectedObj`, T = tracked targets. Types 4 and 8 (TDM heat map, unused) and 13 (`OUTPUT_SNR`) are
never sent by this build. Types 2, 3 and 5 are listed from the build code only (`mss_main.c:1685-1716`);
their contents were not checked on the bench and are not described here.

### 6.4 Payload layouts

Point (type 1), 16 bytes, repeated N times: `float x, y, z, velocity` (metres; m/s). +x is to the right of the
sensor, +y away from it, +z up; positive velocity is moving away. Source: SDK
`ti/datapath/dpif/dpif_pointcloud.h:58-79` (`DPIF_PointCloudCartesian`; this file is in the TI SDK, not in
`src/`; the sizes also follow from `mss_main.c:1665`).

Side info (type 7), 4 bytes per point in the same order: `int16 snr, int16 noise`, units of 0.1 dB
(`dpif_pointcloud.h:95-102`).

Compact point (type 12), 8 bytes per point: `int16 azimSinPhaseQuantized, int16 elevSinPhaseQuantized,
int16 rangeIdx, int16 dopplerIdx` (`src/ti/datapath/dpc/objectdetection/objdethwaDDMA/objectdetection.h:176-189`).
These are indices, not metres; the host needs the cfg (range/Doppler resolution) and the phase scaling to
convert them. The driver reads this type as 12 (`CPSL_TI_Radar_cpp/src/SerialStreamer/UartFrame.cpp:17`).

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
then `uint32 state`. The target ID is not in this struct.

RANSAC mask (type 11): one `uint8` per detected point (`mss_main.c:1739-1741`); the meaning of the
values was not checked.

## 7. Raw ADC (LVDS) is not in this image

`mmw/dss/mmw_lvds_stream.c` and `.h` are set `excludeFromBuild="true"` in
`src/mmwave2chipCascade_dss.projectspec:148-149`, and `LVDS_STREAM` is not defined in either projectspec
or any source. The code behind it is therefore not compiled: the `numLvdsLanesCfg` command
(`mmw_cli.c:2328-2331`) is absent, and there is no `lvdsStreamCfg` command. Nothing in `mmw/` calls
`MmwDemo_LVDSStreamSwConfig` either. The cascade streams point clouds over UART only; a DCA1000 UDP
layout and a per-chirp Rx layout cannot be derived from this build. The earlier text is kept, unverified,
in [`archive/lvds_raw_stream_unverified.md`](archive/lvds_raw_stream_unverified.md).

# SAR feasibility memo: IWR1843, SDK 3.6, 1TX/1RX continuous chirp

What bounds a 1TX/1RX continuous-chirp SAR configuration on the IWR1843 with mmWave SDK 3.6.02.00-LTS, the
LVDS/DCA1000 data-rate math, and go/no-go criteria. Generic: any cfg within the limits qualifies; (e) works one example. Line numbers were checked in the build container: `rl_sensor.h` is
`/opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti/control/mmwavelink/include/rl_sensor.h`; other SDK paths are relative to
`.../packages/ti`; `src/` is this project's. Terms: the HSI header (high-speed-interface
header) is the per-transfer header the DCA1000 expects; CBUFF is the SDK's LVDS streaming driver; a HW session
streams ADC data from the ADC buffer, a SW session streams CPU buffers; MSS is the Cortex-R4F master subsystem.

Symbols: Tc chirp period, Ns samples per RX, R RX channels, Nc chirps per frame, Tb inter-frame blank, `nlane` LVDS
lanes, `Blane` Mbps per lane, `Bchirp` bytes per chirp, v platform speed, d_max along-track limit.

## (a) Limits

| Parameter | IWR1843 limit | Source |
|---|---|---|
| idle (`idleTimeConst`) | 0 to 524287 x 10 ns (5.24 ms) | `rl_sensor.h:653` |
| rampEnd (`rampEndTime`) | 0 to 500000 x 10 ns (5 ms) | `:665` |
| chirp period Tc | idle + rampEnd; minimum cycle 15 us | `:4570` |
| ADC rate (`digOutSampleRate`) | 2000 to 37500 ksps; max IF bandwidth 15 MHz | `:742` |
| complex 1x max rate | 18.75 Msps (regular ADC mode); usable IF about 0.8 x fs (engineering margin) | `:750` |
| samples per RX (Ns) | 2 to MAX. TI's table lists 1024 (4 RX complex) and 2048 (2 RX); **4096 for 1 RX complex (4 B each, 16 KB) is extrapolated, not TI-stated** | `:731-736` |
| ADC buffer | 32 KB total; ping and pong halves (inferred from per-half chirp thresholds) | `ti/common/sys_common_xwr18xx.h:306`; `drivers/adcbuf/ADCBuf.h:512-566` |
| slope (`freqSlopeConst`) | LSB 48.279 kHz/us, range +-2072 (max 100 MHz/us) | `:709-710` |
| sweep band | within 76-78 GHz or 77-81 GHz | `:667` |
| loops, chirp indices, frames | loops 1 to 255; indices 0 to 511; `numFrames` 0 = infinite | `:958`, `:949-953`, `:963` |
| frame period | 300 us to 1.342 s, LSB 5 ns | `:986-987` |
| inter-frame blank Tb | see (c) | `:983`, `:4468` |
| LVDS | 2 lanes x 600 Mbps DDR = 150 MB/s | `src/mss/mmw_lvds_stream.c:154`; `src/mss/mss_main.c:2911-2912` |
| HPF corners | HPF1 175/235/350/700 kHz; HPF2 350/700/1400/2800 kHz | `rl_sensor.h:774-791`; datasheet SWRS228B 7.7 |
| RX gain | even values 24 to 52 in the API; datasheet specifies 24 to 48 dB, so design to 48 | `:832`; datasheet 7.7 |

Nc = loops x (chirp indices used), so more than 255 chirps per frame needs several identical chirp indices.

## (b) Data rate

Bytes per chirp (HW session, ADC format, HSI header on), from TI's note at `src/mss/mss_main.c:366-379`:

    Bchirp = roundup256(Ns * R * 4 + 52)        4 B per complex sample; 52 B = TI's two header structs

The header is not stored in the ADC buffer, so it counts against LVDS capacity only, not the 16 KB half. With `Tc` in us and `Blane` in Mbps, LVDS carries `Tc * nlane * Blane / 8` bytes per chirp; the cfg is
feasible only if `Bchirp <= Tc * nlane * Blane / 8`. Sustained rate is `Bchirp / Tc` MB/s against 150 MB/s. The host driver's
DCA1000 packet is 1472 B with a 10 B header (`CPSL_TI_Radar_cpp/src/DCA1000/DCA1000Handler.cpp:591`), so packets/s =
`Bchirp / Tc / 1462`.

## (c) Frame-boundary design rule

A frame is Nc chirps then a blank: `Tb = framePeriodicity - Nc * Tc`. **Tb_min defaults to 300 us** (TI's "typically",
`rl_sensor.h:983`; its table row says the same and that it includes one calibration/monitoring chirp, `:4611-4616`).
250 us (`:4468`) is TI's floor only when no optional calibration or monitor is enabled and nothing else needs the
blank. TI splits that floor as 100 us frame preparation + 150 us applying calibration updates (`:4468`), but its
calibration table gives the apply step as 100 us (`:4558`); this memo does not resolve the two. Optional calibrations
or monitors that are enabled add their own durations (`:4549-4558`: peak detector 500 us, TX power 800 us, RX gain
30 us, ...).

**Open item (bench).** TI's table lists APLL 150 us + Synth VCO 350 us = 500 us (`:4551-4552`) and says these two always run
internally and "the time required for these calibrations must be allocated" (`:2683-2684`); reports come about every
second (`:2733-2734`). TI does not state whether the 300 us typical blank includes them, nor what happens if the blank is
too short (skipped, deferred, or chirps disturbed): neither `rl_sensor.h` nor `rl_monitoring.h` says. A 1.34 s frame at
Tb = 300 us has no spare time for them. Plan Tb with +500 us (worst case) or confirm on a bench capture that phase and
amplitude are stable across frame boundaries.

Along track, the step across a frame boundary is `Tc + Tb`; all other steps are `Tc`. With platform speed `v` and the
SAR design's along-track limit `d_max` (an input, not computed here):

    v * (Tc + Tb) <= d_max        i.e.  Tc <= d_max / v - Tb

Periodic runtime calibration must be off during capture (`calibPeriodicity` 0 = disabled, default 0, valid 0 or 4-100:
`rl_sensor.h:2721-2725`). The stock demo enables it every 10 frames (`src/mss/mss_main.c:3164-3166`), so SAR firmware must
override that.

## (d) Per-chirp metadata

The HSI header cannot carry per-chirp counters: its application extension is static once sessions run
(`utils/hsiheader/hsiprotocol.h:567-572`). Use the HW session's `CBUFF_DataFmt_ADC_USER` (`drivers/cbuff/cbuff.h:349`): a user
record after each chirp's ADC data, updated by the MSS from the chirp interrupts (CHIRP_START 99, CHIRP_AVAIL 123, FRAME_START 98:
`ti/common/sys_common_xwr18xx_mss.h:352-374`; `MSS_SYS_VCLK` 200 MHz, `:464`, is the candidate timestamp clock). Its bytes add to `Bchirp`.
Fallback only if that fails: a frame-level record in the demo's per-frame SW session (`src/mss/mss_main.c:2618-2642`).

## (e) Worked example (self-contained)

Example inputs (given, not derived): stripmap SAR, one TX and one RX, sweep 77.25-80.75 GHz (3.5 GHz) in a 1500 us ramp
(about 2.33 MHz/us), chirp interval 2 ms (idle 500 us), max range 100 m, v = 0.75 m/s, d_max = 7.11 mm. Beat
frequency at 100 m: `2 * slope * R / c = 1.557 MHz`.

- Out of limits: an ADC rate of 1.557 Msps is below the 2 Msps minimum. Rate the ADC at the instantaneous
  ramp rate, not the average over the chirp interval (1.557 x 1500/2000 = 1.17 Msps).
- Chosen: slope code 48 (2.317 MHz/us, sweep 3.476 GHz, inside 77-81 GHz); ADC 2.2 Msps (usable IF 1.76 MHz covers
  1.55 MHz); Ns = 2.2 Msps x 1.5 ms = 3300 (13,200 B, inside the 16 KB half).
- Data: `Bchirp = roundup256(13,200 + 52) = 13,312 B`; 13,312 B / 2 ms = 6.66 MB/s, 4.4 % of 150 MB/s; about 4550 packets/s.
- Frame: the 1.342 s cap gives Nc <= (1.342 s - Tb) / Tc = 670. For example 3 identical chirp indices x 223 loops = 669 chirps,
  framePeriodicity 1.3383 s (Tb = 300 us); a single index would give only 255.
- Boundary: Tc + Tb = 2.3 ms, v x 2.3 ms = 1.725 mm <= 7.11 mm (margin 4.1x).
- Not exercised: HPF and gain. At this slope a 175 kHz corner is at about 11 m, so they are bench-tuned (criterion 5).

## (f) Go/no-go criteria

Go for a SAR build when all hold:

1. The cfg passes (a), and `Bchirp <= Tc * nlane * Blane / 8` with at least 10x margin against the LVDS capacity.
2. Tb >= Tb_min as defined in (c), with enabled optional calibrations added, and `v * (Tc + Tb) <= d_max`.
3. Periodic calibration is disabled, and a corner reflector shows no phase step at frame boundaries (this also closes
   the open item in (c)).
4. A capture of at least 10 minutes loses no packets and every chirp's metadata counter is consecutive.
5. Per-chirp saturation stays below the agreed threshold at the chosen HPF corners and RX gain.

The 10x margin (1), the 10-minute capture (4) and the saturation threshold (5) are the Firmware role's proposals
pending user confirmation. No-go: any of 1-3 fails (change the cfg), or 4 fails within limits (revisit (d)).

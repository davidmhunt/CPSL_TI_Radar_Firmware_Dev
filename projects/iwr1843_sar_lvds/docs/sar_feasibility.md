# SAR feasibility memo: IWR1843, SDK 3.6, 1TX/1RX continuous chirp

What bounds a 1TX/1RX continuous-chirp SAR configuration on the IWR1843 with mmWave SDK 3.6.02.00-LTS,
the LVDS/DCA1000 data-rate math, and the go/no-go criteria for a SAR build. Generic: any cfg that
satisfies the limits below qualifies. Section (g) works one scenario through them. `rl_sensor.h` is
`/opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti/control/mmwavelink/include/rl_sensor.h`; line numbers were
re-checked in the build container. Other SDK paths are relative to `.../packages/ti`; `src/` is this project's.

## (a) Limits

| Parameter | IWR1843 limit | Source |
|---|---|---|
| idle (`idleTimeConst`) | 0 to 524287 x 10 ns (5.24 ms) | `rl_sensor.h:653` |
| rampEnd (`rampEndTime`) | 0 to 500000 x 10 ns (5 ms) | `:665` |
| chirp period Tc | idle + rampEnd; minimum cycle 15 us (xWR1xxx) | `:4570` |
| ADC rate (`digOutSampleRate`) | 2000 to 37500 ksps; max IF bandwidth 15 MHz | `:742` |
| complex 1x max rate | 18.75 Msps (regular ADC mode); real 37.5 Msps | `:750` |
| usable IF | about 0.8 x fs (engineering margin, not an SDK limit) | design rule |
| samples per RX (`numAdcSamples`) | 2 to MAX; table gives 1024 (4 RX, complex), 2048 (2 RX), so 4096 for 1 RX complex (4 B/sample = 16 KB per chirp) | `:731-736` |
| ADC buffer | `SOC_ADCBUF_SIZE` 32 KB, used as ping and pong halves | `ti/common/sys_common_xwr18xx.h:306`; `drivers/adcbuf/ADCBuf.h:512-566` |
| slope (`freqSlopeConst`) | LSB 48.279 kHz/us, range +-2072 (max 100 MHz/us) | `:709-710` |
| sweep band | within 76-78 GHz or 77-81 GHz | `:667` |
| loops (`numLoops`), chirp indices | 1 to 255; indices 0 to 511; `numFrames` 0 = infinite | `:958`, `:949-953`, `:963` |
| frame period | 300 us to 1.342 s, LSB 5 ns | `:986-987` |
| inter-frame blank | 300 us typical; at least 250 us (100 prep + 150 calibration apply) | `:983`, `:4468` |
| LVDS | 2 lanes x 600 Mbps DDR = 150 MB/s | `src/mss/mmw_lvds_stream.c:154`; `src/mss/mss_main.c:2911-2912` |
| HPF corners | HPF1 175/235/350/700 kHz; HPF2 350/700/1400/2800 kHz (two first-order filters, datasheet) | `rl_sensor.h:774-791`; datasheet SWRS228B 7.7 |
| RX gain | even values 24 to 52 in the API; datasheet specifies 24 to 48 dB, so design to 48 | `:832`; datasheet 7.7 |

Chirps per frame = loops x (chirp indices used), so more than 255 chirps per frame needs several
identical chirp indices. The 1.342 s cap bounds the count: N <= (1.342 s - Tb) / Tc.

## (b) Data rate

Per-chirp bytes (HW session, ADC format, HSI header on), from TI's note at `src/mss/mss_main.c:366-379`:

    Bc = roundup256(N * R * 4 + 52)        N samples per RX, R RX channels, 4 B per complex sample

LVDS carries at most `Tc * n * B / 8` bytes per chirp (n lanes, B Mbps per lane); a cfg is feasible only if
`Bc <= Tc * n * B / 8`. Sustained rate is `Bc / Tc` MB/s against the 150 MB/s capacity. The host driver's DCA1000
packet is 1472 B with a 10 B header (`CPSL_TI_Radar_cpp/src/DCA1000/DCA1000Handler.cpp:591`), so
packets/s = `Bc / Tc / 1462`.

## (c) Frame-boundary design rule

A frame is N chirps then a blank: `Tb = framePeriodicity - N * Tc`, and `Tb >= Tb_min` (default 300 us,
`rl_sensor.h:983`; 250 us is the floor at `:4468`). The blank is unavoidable: it covers frame preparation
(100 us) and applying calibration results to hardware (the always-included row, `:4558`), plus thermal control and
re-triggering (`:980-981`). Advanced frames still need at least 100 us between bursts (`:1074`), so they
only shrink it. Optional calibrations or monitors that are enabled add their own durations to
the blank (table at `:4549-4558`: peak detector 500 us, TX power 800 us, RX gain 30 us, ...); add the ones used.

Along track, the step across a frame boundary is `Tc + Tb`, the other steps are `Tc`. With platform speed `v`
and the SAR design's along-track spacing limit `d_max` (an input from the design, not computed here):

    v * (Tc + Tb) <= d_max        i.e.  Tc <= d_max / v - Tb

Periodic runtime calibration must be off during capture (`calibPeriodicity` 0 = disabled, default 0,
valid 0 or 4-100: `rl_sensor.h:2721-2725`). The stock demo enables it every 10 frames
(`src/mss/mss_main.c:3164-3166`), so the SAR firmware has to override that. APLL and SYNTH calibrations
always run internally and their time must be allocated (`:2683`); TI reports them about every second (`:2733`).
Check for phase steps at frame boundaries on the bench.

## (d) Per-chirp metadata

Counters and timestamps per chirp need an in-band record. The HSI header cannot carry them: its application
extension is static and "should not be changed" once sessions run (`utils/hsiheader/hsiprotocol.h:567-572`),
and it describes the session, not each chirp. Use `CBUFF_DataFmt_ADC_USER` (`drivers/cbuff/cbuff.h:349`): a
user-buffer record sent after each chirp's ADC data (up to 3 user buffers, `hsiprotocol.h:361`), rewritten by the MSS before
each chirp from the chirp interrupts (CHIRP_START 99, CHIRP_AVAIL 123, FRAME_START 98: `ti/common/sys_common_xwr18xx_mss.h:352-374`;
`MSS_SYS_VCLK` 200 MHz, `:464`, is the candidate timestamp clock). Extra bytes per chirp count in `Bc` (b).

## (e) MSS-only image

HW streaming runs on MSS resources (ADCBuf handle, CBUFF, EDMA: `src/mss/mmw_lvds_stream.c:630-693`); the DSS only
runs the point-cloud chain, so a raw-capture image can drop it. `scripts/unix/generateMetaImage.sh` (under
`packages/`) accepts `NULL` for the DSS image (usage text, line 13); the demo initializes mmWave in
isolation mode (`src/mss/mss_main.c:3814`). Expected, not yet demonstrated on a board.

## (f) Every-N-frames fallback (contingent)

The stock demo already has a per-frame SW session (`src/mss/mss_main.c:2618-2642`). If per-chirp records
ever proved infeasible, a per-frame record carrying the frame counter and timestamp, with boundary chirps
placed by arithmetic, would fit there. With the throughput margin in (g) this is not expected to be needed.

## (g) Worked example (self-contained)

Scenario: stripmap SAR, one TX and one RX, sweep 77.25-80.75 GHz (3.5 GHz) in a 1500 us ramp (about 2.33 MHz/us),
chirp interval 2 ms (so idle 500 us), max range 100 m, v = 0.75 m/s, along-track spacing limit d_max = 7.11 mm
(1.5 mm actual step). Beat frequency at 100 m: `2 * slope * R / c = 1.557 MHz`.

- Out of limits: the scenario's ADC rate of 1.557 Msps is below the 2 Msps minimum.
- Chosen: slope code 48 (2.317 MHz/us, sweep 3.476 GHz, inside 77-81 GHz); ADC 2.2 Msps (usable IF 1.76 MHz
  covers 1.55 MHz); N = 2.2 Msps x 1.5 ms = 3300 samples (13,200 B, under 16 KB).
- Data: `Bc = roundup256(13,200 + 52) = 13,312 B`; 13,312 B / 2 ms = 6.66 MB/s, 4.4 % of 150 MB/s;
  about 4550 packets/s.
- Frame: the 1.342 s cap gives N <= 670 chirps. For example 3 identical chirp indices x 223 loops = 669
  chirps, framePeriodicity 1.3383 s (Tb = 300 us); loops are at most 255, so a single index would give only 255.
- Boundary: Tc + Tb = 2.3 ms, v x 2.3 ms = 1.725 mm <= 7.11 mm. Margin 4.1x.
- The scenario's data-rate line (1.17 Msps average, 4.7 MB/s) is inconsistent with its 1.557 Msps
  requirement: 1.17 is the average over the 2 ms interval (1.557 x 1500/2000), but the ADC runs at the
  instantaneous rate during the ramp, which is what the limits constrain.

## (h) Go/no-go criteria

Go for a SAR build when all hold (the numeric thresholds are proposals to confirm):

1. The cfg passes (a): Tc, ADC rate, N, slope, band, loops, period, and `Bc <= Tc * n * B / 8` with at least 10x margin on
   sustained rate (B).
2. Tb >= Tb_min with any optional calibrations added, and `v * (Tc + Tb) <= d_max`.
3. Periodic calibration is disabled, and a corner reflector shows no phase step at frame boundaries.
4. A capture of at least 10 minutes loses no packets and every chirp's metadata counter is consecutive.
5. Per-chirp saturation stays below the agreed threshold at the chosen HPF corners and RX gain.

No-go: any of 1-3 fails (change the cfg), or 4 fails after the cfg is within limits (consider (f)).

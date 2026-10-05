# SAR cfg guide: building a valid cfg for `iwr1843_sar_lvds`

For anyone writing a new 1TX/1RX stripmap-SAR cfg. Work through §3 with your requirements, write the cfg like
`configs/sar_example_2ms.cfg`, run `tools/sar_cfg_check.py`; §6 shows the path on one scenario, §7 pairs common mistakes
with the checker's messages. Limits and rate math: `sar_feasibility.md`; packet format: `lvds_data_format.md`. Symbols: Tc chirp
period, Tb inter-frame blank, Nc chirps per frame, Ns samples per RX, R RX channels, fs ADC rate, S slope, B bandwidth,
v platform speed, d_max along-track spacing limit (input from your SAR design). Tags `[n]`: **Sources**.

## 1 How the cfg is built

- Plain text, one command per line, sent over the CLI port at 115200 baud; a line starting with `%` is a comment [10].
  Order: `dfeDataOutputMode 1` before `profileCfg`/`chirpCfg`/`frameCfg` [9], `profileCfg` before its `chirpCfg`,
  `sensorStart` last.
- **`sensorStart` needs** `adcbufCfg`, `lvdsStreamCfg`, `analogMonitor` **and `calibData`** in the cfg besides the mmWave
  commands, even when unused (`analogMonitor 0 0`, `calibData 0 0 0`); otherwise "Full configuration must be provided" [11].
- **Changing the cfg:** `sensorStop`, `flushCfg`, the full cfg, `sensorStart`: no power cycle, but `channelCfg`, `adcCfg`
  and `lowPower` keep their first-start values [12].
- The CLI converts typed numbers to LSB units in float32 and **truncates** [9]; §4 and the checker use the realized values.
- Use one profile and identical chirps (no per-chirp variations) [13].

## 2 Command reference

The firmware's table (`mmw_cli.c`) plus the SDK CLI extension. "Units" are as typed.

| Command | Fields (units) | Limits and notes |
|---|---|---|
| `dfeDataOutputMode` | `1` | Only 1 (frame); 2/3 rejected at `sensorStart` [1] |
| `channelCfg` | rxMask txMask cascading | `1 1 0` = RX0, TX0. R = bits in rxMask; dataFmt 2 needs Ns x R even. First start only |
| `adcCfg` / `lowPower` | bits fmt / 0 mode | `2 1` = 16 bit, complex 1x / `0 0`. First start only |
| `profileCfg` | id startGHz idle(us) adcStart(us) rampEnd(us) txPwrBackoff txPhase slope(MHz/us) txStart(us) Ns fs(ksps) hpf1 hpf2 gain(dB) | idle 0-5242.87; adcStart 0-40.95; rampEnd 0-5000; slope LSB 48.279 kHz/us; fs 2000-18750 for complex 1x; Ns 2+; hpf1 0-3 = 175/235/350/700 kHz; hpf2 0-3 = 350/700/1400/2800 kHz; gain even 24-52 (datasheet 24-48) [2] |
| `chirpCfg` | startIdx endIdx profile startFreqVar slopeVar idleVar(us) adcStartVar(us) txMask | indices 0-511; use `0 0 0 0 0 0 0 1`; for more than 255 chirps per frame define several identical indices |
| `frameCfg` | startIdx endIdx loops numFrames period(ms) trigger delay(ms) | loops 1-255; numFrames 0 = until `sensorStop`; period 0.3 ms to 1342.177 ms, LSB 5 ns; trigger 1 = software [3] |
| `adcbufCfg` | -1 fmt swap interleave chirpThreshold | `-1 0 1 1 1`: complex (0), non-interleaved, chirpThreshold must be 1 [4] |
| `lvdsStreamCfg` | -1 header dataFmt enableSW | `-1 1 2 0`. dataFmt 0/1/2/4; **2** = ADC + per-chirp metadata, needs complex ADC (`adcbufCfg` fmt 0), non-interleaved (interleave 1) and Ns x R even [5]; enableSW must be 0 |
| `analogMonitor` | rxSat sigImg | `1 0` enables the saturation monitor; sigImg 1 lengthens the blank (§4) |
| `CQRxSatMonitor` | profile satSel sliceDur(0.16 us) numSlices rxMask | `0 3 146 127 0`: 64 primary slices cover 1495 us; sliceDur x 0.16 us x (numSlices+1)/2 must not exceed the ADC window [6] |
| `calibData` | save restore flashOffset | `0 0 0`; save and restore not both 1 |
| `sensorStart [0]`, `sensorStop`, `flushCfg`, `queryDemoStatus`, `sarStats`, `version`, `CQSigImgMonitor` | | `sarStats` = per-run counters, works while running; `CQSigImgMonitor` is not needed |

**Rejected:** stock-demo `guiMonitor`, `cfarCfg`, `multiObjBeamForming`, `calibDcRangeSig`, `clutterRemoval`, `compRangeBiasAndRxChanPhase`,
`measureRangeBiasAndRxChanPhase`, `aoaFovCfg`, `cfarFovCfg`, `extendedMaxVelocity`, `configDataPort` (removed in firmware-07; TI cfgs in `configs/` stop at the first) [7]; `advFrameCfg`, `subFrameCfg`,
`contModeCfg`, `bpmCfgAdvanced` (frame mode only); `lvdsStreamCfg ... 1` (enableSW). Periodic calibration cannot be
enabled: it is off in the firmware [8].

## 3 From requirements to values

1. **Bandwidth.** `dR = c/(2B)`, B the *sampled* bandwidth `S x Ns/fs`: `B >= c/(2 dR)`, plus a few percent (the slope is rounded down, §4).
2. **Slope.** Pick the ADC window `Tw` and `S = B/Tw`; the code is `n = floor(S/0.048279)`, use `S_real = 0.048279 n`
   from here on. The sweep (start + `S_real` x rampEnd) must stay inside 77-81 GHz (or 76-78).
3. **ADC rate.** `fb = 2 S_real R_max / c` must be `<= 0.8 fs`; choose `fs` in 2000-18750 ksps (2000 is the floor even if `fb`
   is tiny), with spare margin.
4. **Samples.** `Ns = fs x Tw`, `Ns x 4 B x R <= 16384`, and Ns x R even for dataFmt 2. If it does not fit, shorten `Tw`
   and redo step 2 with a steeper slope, or lower `fs`.
5. **Ramp end.** `rampEnd = adcStart (about 10 us) + Tw + tail (a few us)`.
6. **Chirp period.** `v x (Tc + Tb) <= d_max` with Tb >= 300 us, so `Tc <= d_max/v - Tb`; take the largest Tc with margin;
   `idle = Tc - rampEnd >= 0`.
7. **Frame.** `Nc = indices x loops`, loops <= 255, several identical chirp indices above 255 chirps;
   `framePeriodicity = Nc x Tc + Tb <= 1342.177 ms` (Nc <= 670 at Tc = 2 ms).
8. **LVDS.** `lvdsStreamCfg -1 1 2 0`: `64 + 4 Ns R + 64` B per chirp, under 10% of `Tc x 150 B/us`.
9. Run `sar_cfg_check.py cfg --max-range R_max --speed v --dmax d_max`; fix every ERROR.

## 4 Timing math

- **Chirp period** `Tc = idle + rampEnd` (10 ns steps) [2]; `adcStart + Ns/fs <= rampEnd`.
- **Slope quantization.** `code = trunc(S x 2^26 / (3.6e3 x 900))`, LSB 48.279 kHz/us, so the realized slope is **at or below**
  the typed one (2.333 gives code 48 = 2.3174 MHz/us, -0.7%). B, `fb`, resolution and the sweep end follow `S_real`.
- **Frame.** `Tb = framePeriodicity - Nc x Tc`. Loops are 1-255 per index, so the 1.342 s period is the real limit:
  `Nc <= (1342.177 ms - Tb)/Tc`.
- **Blank rule.** `Tb >= 300 us` (TI "typically"; 250 us only with no optional calibration or monitor) [3]. Chirps are
  spaced by Tc inside a frame; across a frame boundary the step is `Tc + Tb`, so the design needs `v x (Tc + Tb) <= d_max`.
- **Why the blank exists.** Frame preparation (100 us), calibration updates (150 us), re-trigger [3]; anything run in
  it adds its time (signal/image monitor, TX power cal 800 us); TI runs APLL (150 us) and synth VCO (350 us) calibrations about once a second [3]. **Open item (bench):** it is not documented whether 300 us covers them. Target
  Tb = 300 us; raise it by at most +500 us only if the bench shows phase or amplitude steps at boundaries The checker warns `TB-OPEN` until then.
- Periodic calibration is off in this firmware [8]; boot calibration and one at each `sensorStart` remain.

## 5 HPF corners, gain and dynamic range

Two independent first-order high-pass filters sit before the ADC [2]. A target at range R gives `f = 2 S R / c`, so a corner
`fc` sits at `Rc = fc c/(2 S)`; the pair attenuates by `sum 10 log10(1 + (fc/f)^2)` dB.

| Corner (kHz) | 175 | 235 | 350 | 700 | 1400 | 2800 |
|---|---|---|---|---|---|---|
| Rc at S = 2.317 MHz/us (m) | 11.3 | 15.2 | 22.6 | 45.3 | 90.6 | 181 |

| Pair, attenuation (dB) at 1 / 2 / 5 / 10 / 20 / 50 / 100 m | |
|---|---|
| 175 + 350 kHz | 48 / 36 / 21 / 11 / 5 / 1 / 0 |
| 350 + 700 kHz | 60 / 48 / 33 / 21 / 11 / 3 / 1 |

Below the corners the filters roll off at 40 dB/decade, close to the R^4 echo fall-off (80 / 68 / 52 / 40 / 28 / 12 / 0 dB), flattening the
near-to-far spread; the checker prints these rows for your cfg. Gain is 24-48 dB in 2 dB steps (API: up to 52);
instantaneous dynamic range of the 12-bit ADC is about 76 / 70 / 64 / 52 dB at 24 / 30 / 36 / 48 dB (datasheet Fig. 7-1, approximate). Start at
30 dB with 175 + 350 kHz and tune with the saturation count (`sarStats`; `satSlices` per chirp, `lvds_data_format.md` §3) to zero clipped chirps.

## 6 Worked example

**Scenario** (restated; nothing depends on a scenario file): stripmap, 1TX/1RX, 0.10 m range resolution,
max range 100 m, v = 0.75 m/s, d_max = 7.11 mm, sweep 77.25-80.75 GHz (3.5 GHz) in a 1500 us ramp, chirp interval 2 ms.

| Step | Value |
|---|---|
| 1-2 slope | typed 2.333 MHz/us, code 48 = **2.3174 MHz/us**; sampled B = 2.3174 x 1500 = 3476 MHz, dR = 4.31 cm  |
| 3 rate | `fb(100 m) = 1.546 MHz`; the sheet's 1.557 Msps is below the 2000 ksps floor and 0.8 fs would need 1.93 Msps. **Adjustment 1:** 1.557 -> **2.2 Msps** (usable IF 1.76 MHz; max IF range 113.8 m) |
| 4 samples | The sheet's 2335 samples belong to 1.557 Msps. **Adjustment 2:** 2335 -> **3300** = 2.2 Msps x 1500 us, bandwidth kept; 13 200 B <= 16 384 B |
| 5 ramp | adcStart 10 us + 1500 us + 10 us tail: **rampEnd 1520 us**; sweep ends at 80.772 GHz |
| 6 Tc | Tc 2000 us: **idle 480 us**. Boundary spacing 0.75 x (2000 + 300) us = 1.725 mm <= 7.11 mm (4.1x margin); in-frame 1.5 mm |
| 7 frame | 255 loops x 1 index: Nc = 255, **framePeriodicity 510.3 ms** = 255 x 2.000 + 0.300, **Tb = 300 us**, dead time 0.059% |
| 8 LVDS | 64 + 13 200 + 64 = 13 328 B per chirp = 6.66 MB/s = 4.4% of 150 MB/s (22x margin); HPF 175 + 350 kHz (`0 0`), gain 30 dB: tuning start |

Cfg: `configs/sar_example_2ms.cfg`. Checker (from `firmware_dev/`: `uv run python projects/iwr1843_sar_lvds/tools/sar_cfg_check.py projects/iwr1843_sar_lvds/configs/sar_example_2ms.cfg --max-range 100 --speed 0.75 --dmax 0.00711`):

```
profile: start 77.2500 GHz, slope code 48 = 2.3174 MHz/us (typed 2.333), sweep 77.2500-80.7725 GHz
range resolution 4.31 cm (sampled bandwidth 3476 MHz); max IF range 113.8 m
Tc 2000.00 us (idle 480.00 + rampEnd 1520.00); 255 chirps/frame (1 idx x 255 loops); period 510.3000 ms
Tb 300.00 us; boundary step Tc+Tb 2300.00 us; dead time 0.0588 %
LVDS: 13328 B/chirp, 6.66 MB/s, 4.44 % of 300000 B capacity per Tc
along-track spacing at 0.750 m/s: in-frame 1.500 mm, boundary 1.725 mm (d_max 7.110 mm)
WARN  [TB-OPEN] Tb 300 us leaves no time for the APLL (150 us) + synth VCO (350 us) ...
PASS: 0 violation(s), 1 warning(s)
```

A second scenario (50 m, 10 cm, 2 m/s, d_max 5 mm: slope 1.8 -> code 37, 2000 ksps, Ns 2000, rampEnd 1020, Tc 2000 us, 3 x 200 loops, period 1200.3 ms) is in `test_sar_cfg_check.py`.

## 7 Common mistakes and what the checker says

| Mistake | Checker message (start of line) |
|---|---|
| fs below 2 Msps (the 1.557 Msps Nyquist rate) | `[RATE] digOutSampleRate 1557 ksps is outside 2000-37500 ksps` |
| Ns x 4 B x R over 16 KB (Ns 4100) | `[ADCBUF] numAdcSamples 4100 x 4 B x 1 RX = 16400 B exceeds the 16384 B ADC buffer half` |
| ADC window runs past rampEnd | `[ADCWIN] ADC window ends at adcStart 10.00 + 3300/2.200 Msps = 1510.00 us, after rampEnd 1100.00 us` |
| Sweep leaves 77-81 GHz | `[SWEEP] sweep 79.5000-83.0225 GHz (start + realized slope x rampEnd) is not inside 77-81 or 76-78 GHz` |
| Beat above 0.8 fs at `--max-range` | `[IF] beat frequency at 150.0 m is 2.319 MHz, above 0.80 x fs = 1.760 MHz` |
| framePeriodicity < Nc x Tc + 300 us | `[TB] inter-frame blank Tb = ... = 100.00 us is below the 300 us minimum; framePeriodicity must be at least 510.3000 ms` |
| Frame longer than 1.342 s | `[PERIOD] framePeriodicity 1400.000 ms exceeds the 1342.177 ms maximum` |
| Boundary step above d_max | `[SPACING] boundary step v x (Tc + Tb) = ... = 1.725 mm exceeds d_max 1.500 mm` |
| dataFmt 2 with odd Ns x R, or real ADC output | `[LVDSCFG] ... needs numAdcSamples x RX channels even (3301 x 1)` / `... needs complex ADC output` |
| LVDS load above 10% of capacity | `[LVDS] 2128 B per chirp uses 20.3% of the LVDS link, above 1/10 of capacity` |
| `analogMonitor`/`calibData` missing | `[FULLCFG] calibData is missing; sensorStart needs it even when unused` |
| Stock demo command; `numLoops` 256, `chirpThreshold` 2, `enableSW` 1, `dfeDataOutputMode` 3 | `[CMD] line 47 guiMonitor: stock-demo command removed ...`; `[LOOPS]`, `[ADCBUF]`, `[LVDSCFG]`, `[MODE]` |

Exit 0 = no ERROR (warnings do not fail). Options: `--max-range`, `--speed`, `--dmax`, `--tb-min` (300),
`--if-margin` (0.8), `--lvds-margin` (10; 1 only forbids overflow), `--json`.

## Sources

SDK 3.6.02: `rl_sensor.h`, `rl_monitoring.h` in `control/mmwavelink/include/`; `cli_mmwave.c`, `cli.c` in `utils/cli/src/`; the rest `src/mss/`.
[1] `mmw_cli.c:144`. [2] `rl_sensor.h` idle `:653`, adcStart `:659`, rampEnd `:665`, sweep `:666-667`, slope `:710`, fs `:742-750`, HPF `:773-791`,
gain `:832`; datasheet SWRS228B 7.7. [3] `rl_sensor.h` indices `:949-953`, loops `:958`, period and blank `:983-987`, `:4468`, `:4549-4558`, `:4611-4616`.
[4] `mmw_cli.c:420`. [5] `mmw_cli.c:622,631`; `mss_main.c` `MmwDemo_configSensor`. [6] `rl_monitoring.h:1913-1937`. [7] `README.md` "What changed vs TI".
[8] `mss_main.c`. [9] `cli_mmwave.c:613` (profile), `:807` (frame). [10] `cli.c:157`. [11] `mmw_cli.c:160-168`; `mss_main.c`.
[12] `mmw_cli.c:250-270`; README. [13] `mmw_sar_meta.c`; `sar_feasibility.md` (c).

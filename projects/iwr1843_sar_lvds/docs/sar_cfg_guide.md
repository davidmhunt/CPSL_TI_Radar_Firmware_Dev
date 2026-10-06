# SAR cfg guide: building a valid cfg for `iwr1843_sar_lvds`

For anyone writing a new 1TX/1RX stripmap-SAR cfg. Work through §3 with your requirements, write the cfg like the listing in §1,
run `tools/sar_cfg_check.py`. Limits: `sar_feasibility.md`; packet format: `lvds_data_format.md`. Symbols: Tc chirp period, Tb inter-frame blank (frame period minus Nc·Tc), Nc chirps per frame,
Ns samples per RX, Nrx RX channels, fs ADC rate, S slope (`S_real` after rounding, `n` its integer code), B sampled bandwidth, dR range
resolution, Tw ADC window (the sampled part of the ramp), fb beat frequency, R_max maximum range, v platform speed, d_max along-track
spacing limit (input from your SAR design). dataFmt = `lvdsStreamCfg` payload format; satSlices = per-chirp saturation count (dataFmt 2). Tags `[n]`: **Sources**.

## 1 How the cfg is built

- Plain text, one command per line, sent over the CLI port at 115200 baud; a line starting with `%` is a comment [10].
  Top of every cfg: `sensorStop`, `flushCfg` (harmless on the first start). Then `dfeDataOutputMode 1` before `profileCfg`/`chirpCfg`/
  `frameCfg` [9], `profileCfg` before its `chirpCfg`, `sensorStart` last.
- **`sensorStart` needs** `adcbufCfg`, `lvdsStreamCfg`, `analogMonitor` **and `calibData`** besides the mmWave commands, even when
  unused (`analogMonitor 0 0`, `calibData 0 0 0`); otherwise "Full configuration must be provided" [11].
- **Changing the cfg:** `sensorStop`, `flushCfg`, the full cfg, `sensorStart`: no power cycle, but `channelCfg`, `adcCfg` and `lowPower`
  keep their first-start values [12]. Use one profile and identical chirps [13].
- The CLI **truncates** typed numbers to LSB units [9].

The whole example cfg (`configs/sar_example_2ms.cfg` adds comments); fixed lines first, then the values you derive:

```
sensorStop
flushCfg
dfeDataOutputMode 1
channelCfg 1 1 0
adcCfg 2 1
adcbufCfg -1 0 1 1 1
lowPower 0 0
profileCfg 0 77.25 480 10 1520 0 0 2.333 1 3300 2200 0 0 30
chirpCfg 0 0 0 0 0 0 0 1
frameCfg 0 0 255 0 510.3 1 0
analogMonitor 1 0
CQRxSatMonitor 0 3 146 127 0
calibData 0 0 0
lvdsStreamCfg -1 1 2 0
sensorStart
```

`profileCfg` fields: id, start GHz, idle, adcStart, rampEnd (us), txPwrBackoff 0, txPhase 0, slope, txStart 1 us, Ns, fs, hpf1, hpf2, gain.
`frameCfg`: chirp indices 0..0, 255 loops, numFrames 0 (run until `sensorStop`), period ms, trigger 1 (software), delay 0.

## 2 Command reference

| Command | Fields (units as typed) | Limits and notes |
|---|---|---|
| `dfeDataOutputMode` | `1` | Only 1 (frame); 2/3 rejected at `sensorStart` [1] |
| `channelCfg` | rxMask txMask cascading | `1 1 0` = RX0, TX0; Nrx = bits in rxMask. First start only |
| `adcCfg` / `lowPower` | bits fmt / 0 mode | `2 1` = 16 bit, complex 1x / `0 0`. First start only |
| `profileCfg` | see §1 | idle 0-5242.87; adcStart 0-40.95; rampEnd 0-5000 us; slope LSB 48.279 kHz/us; fs 2000-37500 ksps, **12500 max with complex 1x output (this cfg; IWR1843 datasheet IF 10 MHz; low-power ADC mode, not used here, lowers it further)**; Ns 2+; hpf1 0-3 = 175/235/350/700 kHz; hpf2 0-3 = 350/700/1400/2800 kHz; gain even 24-48 dB (API accepts to 52) [2] |
| `chirpCfg` | startIdx endIdx profile startFreqVar slopeVar idleVar adcStartVar txMask | indices 0-511; use `0 0 0 0 0 0 0 1`; for more than 255 chirps per frame define several identical indices |
| `frameCfg` | startIdx endIdx loops numFrames period(ms) trigger delay(ms) | loops 1-255; period 0.3 to 1342.177 ms, LSB 5 ns; trigger 1 = software [3] |
| `adcbufCfg` | -1 fmt swap interleave chirpThreshold | `-1 0 1 1 1`: complex (0), non-interleaved, chirpThreshold must be 1 [4] |
| `lvdsStreamCfg` | -1 header dataFmt enableSW | `-1 1 2 0`. dataFmt 0/1/2/4; **2** = ADC + per-chirp metadata, needs complex ADC (fmt 0), non-interleaved (1) and Ns x Nrx even [5]; enableSW must be 0 |
| `analogMonitor` | rxSat sigImg | `1 0` enables the saturation monitor; sigImg 1 lengthens the blank (§4) |
| `CQRxSatMonitor` | profile satSel sliceDur(0.16 us) numSlices rxMask | `0 3 146 127 0`: 64 primary slices cover 1495 us; (numSlices+1)/2 x sliceDur x 0.16 us must not exceed the ADC window [6] |
| `calibData` | save restore flashOffset | `0 0 0`; save and restore not both 1 |
| `sensorStart`, `sensorStop`, `flushCfg`, `queryDemoStatus`, `sarStats`, `version` | | `sarStats`: per-run counters, works while running |

**Rejected:** the stock demo's detection commands (removed from this firmware, so a TI cfg stops at the first) [7]. Periodic calibration cannot be enabled [8].

## 3 From requirements to values

1. **Bandwidth.** `dR = c/(2B)`, so `B >= c/(2 dR)`; add 3-5% (the slope is rounded down, §4).
2. **Slope.** Pick `Tw` and `S = B/Tw`; `n = floor(S/0.048279)`, `S_real = 0.048279 n` from here on. The sweep (start + `S_real` x rampEnd) must stay inside 77-81 GHz (or 76-78).
3. **ADC rate.** `fb = 2 S_real R_max / c`; `fs >= max(fb/0.8, 2000 ksps)`: 0.8 fs is the usable IF, 2000 ksps the floor. Round up to a comfortable value (at most 12500 ksps).
4. **Samples.** `Ns = fs x Tw`; need `Ns x 4 B x Nrx <= 16384` (the ADC buffer half; Ns <= 4096 for 1 RX) and Ns x Nrx even. If Ns is too big at
   fs 2000 ksps, shorten Tw (steeper S, larger fb) or use fewer RX.
5. **Ramp end.** `rampEnd = adcStart (about 10 us) + Tw + tail (a few us)`; `Tc = idle + rampEnd` (10 ns steps) [2], so `idle = Tc - rampEnd >= 0`.
6. **Chirp period.** `v x (Tc + Tb) <= d_max` with Tb 300 us (the checker's default `--tb-min`, not a TI hard limit, §4), so
   `Tc <= d_max/v - Tb`; take Tc about 10% below.
7. **Frame.** `Nc = indices x loops`, loops <= 255 (several identical indices above 255 chirps, §2);
   `framePeriodicity = Nc x Tc + Tb <= 1342.177 ms` (Nc <= 670 at Tc = 2 ms).
8. **LVDS.** `lvdsStreamCfg -1 1 2 0`: `64 + 4 Ns Nrx + 64` B per chirp, under 10% of `Tc x 150 B/us`.
9. Run `sar_cfg_check.py cfg --max-range R_max --speed v --dmax d_max`; fix every ERROR.

## 4 Timing math

- **Slope quantization.** `n = trunc(S x 2^26 / (3.6e3 x 900))`, so the realized slope is **at or below** the typed one (2.333 gives code 48 =
  2.3174 MHz/us, -0.7%).
- **Tb** = framePeriodicity - Nc x Tc. TI says a blank of 300 us "typically", 250 us only with no optional calibration or monitor [3]; the checker takes 300 us as its default,
  and each enabled monitor adds its duration. **Open item (bench):** TI also runs APLL (150 us) and synth VCO (350 us) calibrations about
  once a second; it is not documented whether 300 us covers them. Raise Tb by at most
  +500 us only if the bench shows phase or amplitude steps at frame boundaries. The checker warns `TB-OPEN` (exit stays 0) while Tb < 800 us.

## 5 HPF corners, gain and dynamic range

Two independent first-order high-pass filters sit before the ADC [2]. A target at range R gives `f = 2 S R / c`, so a corner `fc` sits at
`Rc = fc c/(2 S)`, and the pair attenuates by `sum 10 log10(1 + (fc/f)^2)` dB. Tables are for the **example slope 2.317 MHz/us**; the
checker prints both for your cfg.

| Corner (kHz) | 175 | 235 | 350 | 700 | 1400 | 2800 |
|---|---|---|---|---|---|---|
| Rc (m) | 11.3 | 15.2 | 22.6 | 45.3 | 90.6 | 181 |

| Pair, attenuation (dB) at 1 / 2 / 5 / 10 / 20 / 50 / 100 m | |
|---|---|
| 175 + 350 kHz | 48 / 36 / 21 / 11 / 5 / 1 / 0 |

Below the corners the filters roll off at 40 dB/decade, close to the R^4 echo fall-off. **Criterion:** the farthest range of interest should lie above the higher corner's Rc, or its attenuation in the table (0-5 dB past 20 m
for 175 + 350 kHz) must be accepted; the checker does not enforce it. Gain is 24-48 dB in 2 dB steps; instantaneous
dynamic range of the 12-bit ADC is about 76 / 70 / 64 / 52 dB at 24 / 30 / 36 / 48 dB (datasheet Fig. 7-1, approximate). Start at 30 dB with
175 + 350 kHz and tune with the saturation count (`sarStats`; `satSlices`, `lvds_data_format.md` §3) to zero clipped chirps.

## 6 Worked example

Stripmap, 1TX/1RX, 0.10 m resolution wanted, R_max 100 m, v = 0.75 m/s, d_max = 7.11 mm, 77.25 GHz start, 3.5 GHz
sweep over a 1500 us ramp, chirp interval 2 ms.

| Step | Value |
|---|---|
| 1-2 slope | typed 2.333 MHz/us, n = 48, **S_real = 2.3174 MHz/us**; B = 2.3174 x 1500 us = 3476 MHz, dR = 4.31 cm |
| 3 rate | `fb(100 m) = 1.546 MHz`; at the Nyquist rate (fb) that is 1.55 Msps, below the 2000 ksps floor, and 0.8 fs would need 1.93 Msps. Take **2.2 Msps** (usable IF 1.76 MHz, max IF range 113.8 m) |
| 4 samples | 2.2 Msps x 1500 us = **3300** (13 200 B <= 16 384 B), Nrx 1 so even |
| 5 ramp | adcStart 10 + 1500 + 10 tail: **rampEnd 1520 us**; sweep ends at 80.772 GHz |
| 6 Tc | Tc 2000 us: **idle 480 us**. Boundary 0.75 x 2300 us = 1.725 mm <= 7.11 mm (4.1x); in-frame 1.5 mm |
| 7 frame | 255 loops x 1 index: Nc 255, **period 510.3 ms** = 255 x 2.000 + 0.300, **Tb 300 us**, dead time (Tb / period) 0.059% |
| 8 LVDS | 64 + 13 200 + 64 = 13 328 B per chirp = 6.66 MB/s = 4.4% of 150 MB/s; HPF 175 + 350 kHz, gain 30 dB (tuning start) |

Checker on the §1 cfg (`cd firmware_dev && uv run python projects/iwr1843_sar_lvds/tools/sar_cfg_check.py projects/iwr1843_sar_lvds/configs/sar_example_2ms.cfg --max-range 100 --speed 0.75 --dmax 0.00711`)
prints `PASS: 0 violation(s), 1 warning(s)` (the `TB-OPEN` warning), exit 0.

## 7 Common mistakes and what the checker says

Each row changes one thing in the §1 cfg; message hints omitted. Exit 0 = no ERROR. Options: `--max-range`, `--speed`, `--dmax`,
`--tb-min` (300), `--if-margin` (0.8), `--lvds-margin` (10), `--json`.

| Mistake | Checker message (start of line) |
|---|---|
| fs under 2000 ksps, or over 12500 with complex output | `[RATE] digOutSampleRate 1557 ksps is outside 2000-37500 ksps` / `... exceeds 12500 ksps, the complex 1x maximum` |
| Ns x 4 B x Nrx over 16 KB (Ns 4100) | `[ADCBUF] numAdcSamples 4100 x 4 B x 1 RX = 16400 B exceeds the 16384 B ADC buffer half` |
| ADC window runs past rampEnd (rampEnd 1100) | `[ADCWIN] ADC window ends at adcStart 10.00 + 3300/2.200 Msps = 1510.00 us, after rampEnd 1100.00 us` |
| Sweep leaves 77-81 GHz (start 79.5) | `[SWEEP] sweep 79.5000-83.0225 GHz (start + realized slope x rampEnd) is not inside 77-81 or 76-78 GHz` |
| Beat above 0.8 fs (`--max-range 150`) | `[IF] beat frequency at 150.0 m is 2.319 MHz, above 0.80 x fs = 1.760 MHz` |
| Period under Nc x Tc + 300 us (510.1) | `[TB] inter-frame blank Tb = ... = 100.00 us is below the 300 us minimum; framePeriodicity must be at least 510.3000 ms` |
| Frame longer than 1.342 s (period 1400) | `[PERIOD] framePeriodicity 1400.000 ms exceeds the 1342.177 ms maximum` |
| Boundary step above d_max (`--dmax 0.0015`) | `[SPACING] boundary step v x (Tc + Tb) = ... = 1.725 mm exceeds d_max 1.500 mm` |
| dataFmt 2 with odd Ns x Nrx, real or interleaved ADC data | `[LVDSCFG] ... needs numAdcSamples x RX channels even (3301 x 1)` / `... needs complex ADC output` / `... non-interleaved` |
| LVDS load above 10% of capacity | `[LVDS] 2128 B per chirp uses 20.3% of the LVDS link, above 1/10 of capacity` |
| Stock demo command; `numLoops` 256, `chirpThreshold` 2, `enableSW` 1, `dfeDataOutputMode` 3 | `[CMD] line 47 guiMonitor: stock-demo command removed ...`; `[LOOPS]`, `[ADCBUF]`, `[LVDSCFG]`, `[MODE]` |

## Sources

SDK 3.6.02 `control/mmwavelink/include/` (`rl_*.h`), `utils/cli/src/` (`cli*.c`); others `src/mss/`.
[1] `mmw_cli.c:144`. [2] `rl_sensor.h` idle `:653`, adcStart `:659`, rampEnd `:665`, sweep `:666-667`, slope `:710`, fs `:742-750`, HPF `:773-791`,
gain `:832`; datasheet SWRS228B 7.7. [3] `rl_sensor.h` indices `:949-953`, loops `:958`, period and blank `:983-987`, `:4468`, `:4549-4558`, `:4611-4616`.
[4] `mmw_cli.c:420`. [5] `mmw_cli.c:622,631`; `mss_main.c` `MmwDemo_configSensor`. [6] `rl_monitoring.h:1913-1937`. [7] `README.md` "What changed vs TI".
[8] `mss_main.c`. [9] `cli_mmwave.c:613` (profile), `:807` (frame). [10] `cli.c:157`. [11] `mmw_cli.c:160-168`; `mss_main.c`.
[12] `mmw_cli.c:250-270`; README. [13] `mmw_sar_meta.c`; `sar_feasibility.md` (c).

# Tuning guide: gain, HPF corners and capturing a run you can trust

Pick `rxGain` and the two high-pass corners so that nothing clips and the far end of the swath stands above the noise. Run commands
from `firmware_dev/`. The radar needs a valid cfg ([`sar_cfg_guide.md`](sar_cfg_guide.md)). **HW** marks commands that move hardware.
Formats: [`lvds_data_format.md`](lvds_data_format.md).

## 1 Capture requirement (read this before any capture)

> **One run per recording. Arm the DCA1000 *before* `sensorStart`. Stop it *after* `sensorStop`. Keep the DCA1000 UDP headers
> (`dca_capture.py` does). Read `sarStats` after `sensorStop`, before the next `sensorStart`.**
> Why: a chirp's record is found by its *position* in the byte stream, and the packet's other record slot holds the neighbouring
> chirp's record, which looks equally valid, so a recording that misses the run's first packet can validate yet be off by one chirp,
> silently. Only the DCA1000 byte counts and `sarStats` prove the start is there.
> **If any check below fails or cannot be evaluated, discard the recording and capture again.**

## 2 What gain and the two HPFs do

`rxGain` (24-48 dB, even) amplifies the IF before the 12-bit ADC: more gain lifts weak far returns above the noise but clips strong
near ones. Two first-order high-pass filters (HPF1 175/235/350/700 kHz, HPF2 350/700/1400/2800 kHz) also sit before it. A target at
range R has beat frequency 2SR/c, so a corner fc maps to Rc = fc·c/(2S): 11.3 m (175 kHz) and 22.6 m (350 kHz) at slope 2.317 MHz/us. Below its corner each filter rolls off at 20 dB/decade, so the pair gives 40 dB/decade, countering
the R⁴ excess of near echoes: 175 + 350 kHz attenuates 48 / 36 / 21 / 11 / 5 / 1 dB at 1 / 2 / 5 / 10 / 20 / 50 m
([`sar_cfg_guide.md`](sar_cfg_guide.md) §5).

## 3 The commands

Once: `uv sync --group tools` (numpy, matplotlib). `CFG` is your cfg, e.g. `projects/iwr1843_sar_lvds/configs/sar_example_2ms.cfg`. To send it
(or change gain/HPF) and capture in one go, run a one-point sweep: `--gains 30 --hpf 175:350` (HPF1:HPF2 in kHz, values in §2). It cfg-checks,
sends the cfg, then captures, parses and reports. `dca_capture.py` alone needs a radar that already holds the cfg.

```bash
T=projects/iwr1843_sar_lvds/tools
uv run python $T/dca_capture.py run1.cap --cli-port /dev/ttyACM0 --duration 5   # HW
uv run python $T/sar_parse.py run1.cap --cfg CFG      # checks 1-4, run1_adc.bin, run1_meta.csv
uv run --group tools python $T/sar_tune_report.py run1 --cfg CFG --reflector-range 1.5
uv run --group tools python $T/sar_tune_sweep.py CFG out1 --cli-port /dev/ttyACM0 \
    --gains 30 --hpf 175:350 --reflector-range 1.5                               # HW: one point
```

`dca_capture.py` arms the DCA1000, sends `sensorStart`, waits, sends `sensorStop`, reads `sarStats`, then stops recording; without
`--cli-port` it arms and asks you to type those three commands and enter `chirpAvail`. DCA1000 address, port and `--timer-s` options: README. The report also writes `run1_tune.png`. A sweep needs no power cycle (several points: comma lists; `--dry-run` only cfg-checks).

## 4 `sarStats`: the number that proves the run's length

Type `sarStats` in the radar CLI after `sensorStop` (`dca_capture.py` stores it as `run1.cap.sarstats.json`):

```
mmwDemo:/>sarStats
run 1 (sensor state 0), dataFmt 2, satMon 1
chirps 5100 frames 20 chirpStartIsr 5100 chirpAvail 5100  <-- packets sent in this run
saturatedChirps 4
```

(Illustrative values.) If `sensorStop` prints `no BSS frame-end event after sensorStop`, discard the run.

## 5 The four checks and the discard policy

`sar_parse.py` prints one line per check, then **`VERDICT: ACCEPTED`** or **`VERDICT: REJECTED: capture requirement not met`** naming the
failing checks. A rejected capture writes no output files (`--force` does, with a warning, for debugging only).

| # | The check | A typical failure means |
|---|---|---|
| 1 | Byte counts consistent: no overlapping datagrams, no restart at 0 (lost, reordered, exact duplicate datagrams are fine) | two recordings joined |
| 2 | At least 99 % of packets with all record bytes present validate (record equals its position; max(1, 1 %) failures allowed); every record carries the run's `runIdx` | began mid-packet or 2+ packets late; a second run in the file; "firmware lost count" if failures persist |
| 3 | In the last packet (`chirpAvail` − 1) the *other* slot does not hold the run's next chirp | began one packet late (check 2 cannot see it) |
| 4 | Bytes recorded = `chirpAvail` × B (B = packet size, printed); a lost final datagram is a tolerated tail hole (check 4 then decides check 3) | short at the start or end, or a second run appended; **no `sarStats` reading: fails** |

*Not evaluable* counts as a failure, except check 3 when a lost final datagram held the last record slots: then check 4 decides. Checks 3 and 4 await
hardware confirmation (measured on the bench; see README Status).

## 6 Bench procedure

1. Put a **near reflector at 1-2 m** and run the one-point sweep (§3) at **gain 30 dB, HPF 175:350**.
2. Capture, parse (§3); go on only if `ACCEPTED`. Run the report with `--reflector-range`.
3. **If chirps clip**, step the HPF up first (re-run with `--hpf 350:700`). Lower the gain only if it still clips.
4. **Raise the gain** until the swath's far end is clearly above the noise floor with **clipped chirps 0 for the whole capture**.
5. Record the chosen point (gain, HPF pair, report numbers, cfg). `sar_tune_sweep.py` tabulates steps 2-4.

## 7 Reading the report and the sweep table

| Report line | Meaning |
|---|---|
| `peak ... (dBFS)` | largest \|I\| or \|Q\| per chirp vs full scale (12-bit: 2048 counts = 0 dBFS): max, 99th percentile, median. Max near 0 = no headroom. `WARNING ... exceed full scale`: ADC not 12-bit as assumed (`--adc-bits`) |
| `clipped chirps` | each chirp counted once if **ADC full-scale hit** (samples at the rails) or **firmware saturation** (`satSlices` > 0). Target 0. "Firmware result unknown": no answer for that chirp (the run's last, or its record was lost); not the same as clean |
| `noise floor` | median of the mean range profile over a quiet far region (default: last fifth of the usable bins; `--noise-range A B`); keep targets out |
| `mean range profile`, `HPF` | strongest bin, its height over the noise floor; HPF corner ranges and attenuation at 1-50 m. The image plots the profile against range, HPF response dashed (right axis) |
| `summary` (`sar_parse`) | chirps; packets wholly absent / ADC partly lost (marked, never shifted); records valid; LATE-flagged (interrupt late, record may be missing); saturated chirps (firmware monitor only, lag applied); in-frame / boundary dt vs Tc and Tc+Tb (mean, p99.9; a boundary excess means the frame gap varies) |
| `reflector near R` | peak within ±0.25 m of `--reflector-range`; SNR over the noise floor |

**Lag-1 saturation.** The saturation monitor delivers a chirp's result only once its ADC samples are complete, when its packet starts
sending, but the packet's record was written earlier, at chirp start. So chirp n's result arrives in chirp n+1's record (`satRefLag` = 1;
sometimes 2 or 0). The tools subtract the lag: the clipped count and `sat_slices_this_chirp` (`_meta.csv`) name the chirp that saturated. `satSlices` = how many of up to 64 slices of the ADC window saw saturation. 

Sweep table: one row per point, same quantities; `REJECTED` = a check failed (see `note`, redo it), `SKIPPED` = the cfg checker refused it.

## 8 Pitfalls

- **DC and near-range leakage.** A peak in the first bins that ignores the reflector is leakage: raise the HPF.

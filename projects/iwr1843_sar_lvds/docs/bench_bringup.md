# Bench bring-up: flash the IWR1843BOOST and run the first check

Numbered steps for the person at the bench, with what you should see and what to do if you do not. Steps marked
"(untested)" are unconfirmed on hardware; sections 3 to 5 (flash, run mode, first check) are confirmed on one board (2026-10-06). Paths are relative to `firmware_dev/` unless noted.

## 1 Before you start

Bring: the IWR1843BOOST, its 5 V supply, a micro-USB cable, the DCA1000EVM with its own 5 V supply, the LVDS cable or
mounting that joins the two boards (per the DCA1000 guide), and an Ethernet cable from the DCA1000 to the host.

- Image: `projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin`, 152132 bytes, sha256
  `53948f4d20ca501490dd3d1dbe48229622af5346914dfe33daec33980864267a` (built 2026-10-05T23:51Z from `firmware_dev` commit 946f48d).
  Check: `sha256sum projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin`. A different hash means a different build: stop and ask.
- Host: your user must be in the `dialout` group (`id -nG | grep dialout`; if missing, add it and log in again).
- Ports, once the board is powered and plugged in: `ls /dev/serial/by-id/`. `...XDS110...-if00` is the CLI port
  (115200 baud, used here); `...-if03` is the data port (unused). Use the by-id names: `/dev/ttyACM*` numbers can change.

## 2 Host network (DCA1000)

The DCA1000 uses its factory addresses: FPGA `192.168.33.180`, host NIC `192.168.33.30/24`, command port 4096, data port 4098.
The tools in `projects/iwr1843_sar_lvds/tools/` default to exactly these.

1. Connect the DCA1000 to the host Ethernet port and power it (5 V). The FPGA/power LEDs light.
2. Give the host NIC the address: `ip addr` should list `192.168.33.30/24` on the wired interface cabled to the DCA1000. If not,
   add it (NetworkManager profile or `sudo ip addr add 192.168.33.30/24 dev <nic>`); `CPSL_TI_Radar_cpp/Readme.md`
   (repository root) covers host prerequisites, including the UDP receive-buffer limit.
3. `ping -c 3 192.168.33.180` may or may not answer (a bench ping failed; not known to be harmless). The real checks are `ip addr`
   above, the DCA1000 link LED, and the capture tool in section 5. If the capture sees nothing: check the cable, link LED, power.
4. DCA1000 switches: for the network address, SW2.6 must be at position 11 (default FPGA addresses; position 6 loads
   whatever is saved in the EEPROM), per `DCA_Programming/README.md` (repository root). For the LVDS capture mode, see the
   DCA1000EVM user guide. Do not change switches with power on.

## 3 Flash (SOP0 + SOP2 closed)

Confirmed on hardware (2026-10-06, one IWR1843BOOST, this command): the flashed image boots to the `mmwDemo:/>` prompt in SOP 001. The stock-demo restore is not yet confirmed.
Route notes: [`docs/research/iwr1843_headless_flash_2026-10-06.md`](../../../../docs/research/iwr1843_headless_flash_2026-10-06.md).

1. SOP switch S1 (SOP2, SOP1, SOP0 left to right, ON = up; `readme_images/IWR1843_SOP_nodes.png`, repository root): flash mode
   is SOP2 and SOP0 ON, SOP1 OFF (101). Close every terminal and viewer on the port.
2. **Power-cycle fully (unplug USB and the 5 V supply, replug both) before EVERY attempt, retries included.** A retry without it
   failed with `XXXX Received unexpected data!!!XXXX` / `Not able to connect to serial port`.
3. From `firmware_dev/`, dry run first:
   `./fw flash iwr1843_sar_lvds /dev/serial/by-id/<...XDS110...-if00> --dry-run` (prints the command and image sha256; compare with section 1).
   Then the same without `--dry-run`, in your own terminal; type `FLASH MODE CONFIRMED` when asked (it refuses without a TTY).
4. Success: `SUCCESS!! File type META_IMAGE1 downloaded successfully to SFLASH.` (after `Erase storage completed successfully!`).
   Observed: exit code 0 (`Flashed (DSLite rc=0)`) and no trailing `Can't Run Target CPU` line. On failure: keep `build/flash_output.log`,
   power-cycle fully, retry once, then stop and report.
5. Restore the stock demo (also the recovery; the flash formats all SFLASH; untested): power-cycle in SOP 101, then the same command with
   `projects/ti_stock_demos/build/iwr1843_demo.bin` as the image argument after the port.
6. Fallback only if `./fw flash` fails repeatedly: UniFlash GUI "Generate Package" (device IWR1843, Meta Image 1 = the image, COM port).
7. Power off and set SOP2 OFF (SOP0 stays ON, 001). Success is confirmed only in section 4 (the `mmwDemo:/>` prompt).

## 4 Run mode (SOP0 only)

1. Power on with only SOP0 ON (SOP[2:0] = 001, functional mode).
2. Open the CLI port at 115200 baud (for example `picocom -b 115200 /dev/serial/by-id/...-if00`). Press Enter: the prompt
   `mmwDemo:/>` appears. If nothing appears: check the by-id port, that SOP2 is really OFF, and power-cycle once.

## 5 First bring-up check

A cfg is accepted once per power-up: power-cycle the board before this step if it has already been configured since power-on.
Run from `firmware_dev/`; `bench_check.py` finds the CLI port itself (the single `*XDS110*-if00`; `--cli-port` overrides) and
prints each reply in full plus a PASS/FAIL line:
```bash
BC="uv run python projects/iwr1843_sar_lvds/tools/bench_check.py"
```

1. `$BC cfg` sends the cfg line by line (confirmed). Every line must end with `Done`; the last line is `sensorStart`.
   It stops at the first `Error`: keep the output and report it.
2. `$BC status`: sensor running, `chirps` and `frames` increasing over 5 s (`--wait`), `chirpStartIsr` equal to `chirps`.
3. `$BC stop` (`sensorStop`): must not print `no BSS frame-end event after sensorStop`.
4. Capture, which is also the reconfigure test: no power cycle since step 1. `$BC capture [--duration 5]` sends the cfg without
   its last line (the capture tool sends `sensorStart` itself), then runs `dca_capture.py` and `sar_parse.py`
   (the same as `dca_capture.py /tmp/bringup.cap --cli-port <CLI> --duration 5`, then `sar_parse.py /tmp/bringup.cap --cfg <cfg>`)
   and prints the VERDICT and `chirpAvail` from `/tmp/bringup.cap.sarstats.json`.
   Pass: every line of the second cfg is acked, the parser prints checks 1 to 4 as passing and `VERDICT: ACCEPTED`, and
   `chirpAvail` is 2295 or 2550 (9 or 10 frames of 255 chirps at 510.3 ms; record it).
   Meaning of each check: `docs/tuning_guide.md` section 5. On `REJECTED`, keep both files and report the failed check.
   No datagrams at all: recheck section 2 and the DCA1000 switches. The capture options (`--fpga-ip --host-ip --cmd-port
   --data-port`) default to the factory addresses.
5. The tool ends the run with `sensorStop`; `sarStats` counters reset at every `sensorStart`, so they are from the capture run.

## 5b Bench runs (firmware-10 Step 2.2 and 2.6)

After section 5 passed, from `firmware_dev/` with the board in run mode (no power cycle needed between these; each ends with
`RESULT: PASS|FAIL` and a short block to paste back; captures and cfgs go to `/tmp/bench_run/`). Source: `tools/bench_run.py`.

| Command | What it checks |
|---|---|
| `./bench long` | 60 s capture: not cut off by `--timer-s 30`, bytes = `chirpAvail` x B, 0 UDP sequence gaps (CONFIG_PACKET_DATA delay unit) |
| `./bench restart` | 4 cycles of sensorStop, flushCfg + cfg with changed rxGain/HPF (channelCfg, lowPower, adcCfg identical), sensorStart, 30 s capture, parse |
| `./bench chan` | full cfg with `channelCfg` changed (every line answers Done: the CLI only stores it), then `sensorStart`: expect `Error: channelCfg differs from the first sensorStart`, Sensor State not 2, no Exception; then the original cfg + capture still works |
| `./bench finite` | `numFrames 5`: run ends by itself, `sensorStop` still works, `chirpAvail` = 5 x 255, `LVDS HW frames done` = 5 |
| `./bench start0` | `sensorStart 0` restart with no new cfg, capture + parse |
| `./bench adc` | gain 48 capture (reflector close, clipping): peak I and Q counts and the `--adc-bits` it implies (12 or 16); `--capture FILE` re-analyses an old capture |

Set A (no reflector needed; firmware-10 Steps 2.2 and 2.6; `tools/bench_a.py`, analysis in `tools/bench_stream.py`):

| Command | What it checks | min |
|---|---|---|
| `./bench bytes` | 2.6.2 raw byte order (HSI id `DC 0A DC 0A DA 0C DA 0C` at packet start, `"SA" 01 00 "RM"` at M and M+32, unscramble gives `SARM`, every slot k mod 2 validates) and 2.6.4 other-slot regime (k+1 or k-1, >= 1000 packets, last packet of each frame excluded). `--capture FILE` re-analyses an old capture with no board: `./bench bytes --capture /tmp/bench_run/long.cap` | 1 |
| `./bench irq` | 2.6.1: `sarStats` after `sensorStop`, 2 reads 4 s apart, one more with the cfg re-sent (before the next `sensorStart`): `chirpStartIsr` = `chirpAvail` = `chirps`, nothing moves | 1 |
| `./bench bsize` | 2.6.3: B for header on (13328), header off (13264), and Ns 3302 (R*Ns = 2 mod 4: H 56, B 13328; cfg-checked), 10 s each | 2 |
| `./bench late` | 2.6.9: a good capture, then offline copies (first packet cut and renumbered; one datagram dropped), then a capture armed 2 s after `sensorStart` | 2 |
| `./bench fmt1` | 2.6.7: dataFmt 1 (`-1 1 1 0`) 30 s: 0 gaps, stock frame size, offline decode with the C++ converter's pairing, LVDS frame count, `sarStats`, then back to dataFmt 2 | 2 |
| `./bench fmt4` | 2.2: dataFmt 4 with CQ monitors on (`analogMonitor 1 1`, `CQSigImgMonitor 0 111 4`), then off, then back to dataFmt 2; sizes, 0 gaps, no frame-end warning, LVDS frame count. dataFmt 4's block contents are NOT parsed (no parser path) | 2 |

### 5c Order for Set A (board in run mode, DCA1000 up; no physical action; about 10 min)

Run from `firmware_dev/`, in this order. Each re-sends its own cfg (reconfigure after `sensorStop` + `flushCfg`, the same boot as the earlier cycles).
Stop and report if one FAILs. Power-cycle only if a command left the CLI dead (`fmt4` is last for that reason).
```
./bench bytes --capture /tmp/bench_run/long.cap     # offline, no board: the 60 s capture from ./bench long
./bench bytes
./bench irq
./bench bsize
./bench late
./bench fmt1
./bench fmt4
```
Paste each block back. If `fmt4` shows frame or `sensorStart` errors with CQ on, rerun with `--tb-add-us 500` (the signal/image monitor adds time to the blank) and report both.

Set B (a corner reflector at a MEASURED range; `tools/bench_b.py`; each prints one `RESULT` line per goal G1-G7 where it applies, then `RESULT: PASS|FAIL`):

| Command | What it does | min |
|---|---|---|
| `./bench adc` | (before the others) writes `/tmp/bench_run/adc_bits.txt` once it decides 12 or 16 bit; Set B reads it, or take `--adc-bits N` | 1 |
| `./bench tune --range M` | Step 2.3: gain 24/30/36/42 x HPF 175:350 and 350:700, 5 s each (`--gains`, `--hpf`, `--duration` change it); table of ADC-clipped chirps, firmware saturation flags, noise, reflector SNR; prints the candidates (0 clipped) and the chosen point with the next command | 3 |
| `./bench sat --range M --gain G --hpf A:B` | 2.6.6: >= 5 gains from the tuned G up (steps of 4) to clipping, 30 s each; firmware `saturatedChirps` vs parser vs ADC-clipped, non-decreasing, 0 at G, > 0 at the top, lag alignment vs no lag | 5 |
| `./bench endurance --range M --gain G --hpf A:B` | Step 2.4: 30 s cross-check through `sar_parse` + `sar_tune_report`, then 600 s with `sarStats` each minute, `sensorStop`, 2 more reads and one before the next start; streaming analysis (the 4 GB capture does not fit `sar_parse`'s in-memory design). G1, G2, G3, G4, G6 (Step 2.5), G7: 2.6.1, .2, .4, .5, .8 | 15 |
| `./bench soak` | NO reflector, no `--range/--gain/--hpf`: the 30 s pre-check, then 600 s of the example cfg as shipped (default gain/HPF, `analogMonitor`/`CQRxSatMonitor` on), `sarStats` each minute, post-stop reads. Needs >= 8 GB free in `/tmp/bench_run/` (else `RESULT: ABORTED`, nothing sent). Evaluates only G1, G2 (incl. p99.9), 2.6.1/.2/.4/.5/.8; saturation counters recorded, not judged; G3, G4, G6 and reflector rows print `NOT EVALUATED (no reflector) -> firmware-18`. Writes `/tmp/bench_run/soak_summary.json` + `soak_summary.txt` (<= 40-line paste block). ~13 min, ~4 GB: delete with `rm -f /tmp/bench_run/soak.cap /tmp/bench_run/soak.cap.sarstats.json /tmp/bench_run/endurance_pre*` | 13 |
| `./bench tb --add-us N --range M --gain G --hpf A:B` | Step 2.5 follow-up (only if endurance reports a step): 60 s with Tb = 300 + N us (N 100..500), same step statistics | 2 |

### 5d Order for Set B (reflector needed)

Physical: a corner reflector, static (no wind, no people moving near the beam), and a tape measure; the boresight range M in metres.
Capture files go to `/tmp/bench_run/` (the 10 min capture is about 4 GB; delete it afterwards).
```
# 1. reflector at 1-2 m, close enough to clip at the default 48 dB (./bench adc takes --gain N to change it)
./bench adc                                   # 1 min; prints "saved ... --adc-bits N"
./bench tune --range 1.5                      # 3 min; use the real range; note CHOSEN gain G and HPF A:B
./bench sat --range 1.5 --gain G --hpf A:B    # 5 min; the top gain must clip: move the reflector closer if it does not
# 2. move the reflector to a measured 3-10 m, do not touch it for the next 20 minutes
./bench endurance --range M --gain G --hpf A:B   # 15 min
# 3. only if the output says "G6 follow-up": steps at Tb = 300 us
./bench tb --add-us 100 --range M --gain G --hpf A:B     # then 200 ... up to 500 until clean
```
Add `--adc-bits N` to each if `./bench adc` was not run. Paste the whole output of each command.

## 6 Hand-off: what to record

Write down: the by-id port names, the host and DCA1000 addresses and ports used, the image sha256 flashed, the exact flash
method and output, each check's result, the `sarStats` and `chirpAvail` numbers, and anything unexpected (extra prompts, LED
states, error text). Power down, restore the switches as found, and tell whoever tracks bench use the boards are free.

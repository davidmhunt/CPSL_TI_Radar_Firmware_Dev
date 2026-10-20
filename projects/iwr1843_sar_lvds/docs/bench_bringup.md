# Bench bring-up: flash the IWR1843BOOST and run the first check

The full procedure for the person at the bench; the short version is [`bench_check.md`](bench_check.md). Paths are relative to `firmware_dev/`.

## 1 Before you start

Bring: the IWR1843BOOST with its 5 V supply and micro-USB cable, the DCA1000EVM with its 5 V supply, the LVDS cable or mounting
that joins the two boards, and an Ethernet cable from the DCA1000 to the host.

- Image: `projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin`, 152132 bytes, sha256
  `a06ee615b56b12282f2fe9b4a02bb556ceaa81a1b72176e2060cd9209a070924` (current image, `firmware_dev` commit 119de4f; the earlier 53948f4d image was built from 946f48d).
  Check: `sha256sum projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin`. A different hash means a different build: stop and ask.
- Host: your user must be in the `dialout` group (`id -nG | grep dialout`; if missing, add it and log in again).
- Terminal: `picocom` (`sudo apt install picocom`), or without installing (exit with Ctrl-]):
  `uv run python -m serial.tools.miniterm <by-id port> 115200`.
- Ports: `ls /dev/serial/by-id/`. `...XDS110...-if00` is the CLI port (115200 baud, used here); `...-if03` is the data port (unused).
  Use the by-id names: `/dev/ttyACM*` numbers can change.

## 2 Host network (DCA1000)

The DCA1000 uses its factory addresses: FPGA `192.168.33.180`, host NIC `192.168.33.30/24`, command port 4096, data port 4098 (the tools default to these).

1. Connect the DCA1000 to the host Ethernet port and power it (5 V). The FPGA/power LEDs light.
2. `ip addr` should list `192.168.33.30/24` on the wired interface cabled to the DCA1000. If not, add it (NetworkManager profile or
   `sudo ip addr add 192.168.33.30/24 dev <nic>`); `CPSL_TI_Radar_cpp/Readme.md` (repository root) covers host prerequisites,
   including the UDP receive-buffer limit.
3. `ping -c 3 192.168.33.180` may or may not answer (a bench ping failed; not known to be harmless). The real checks are `ip addr`,
   the DCA1000 link LED, and the capture in section 5. If the capture sees nothing: check the cable, link LED, power.
4. DCA1000 switches: SW2.6 at position 11 gives the default FPGA addresses (position 6 loads whatever is saved in the EEPROM), per
   `DCA_Programming/README.md` (repository root). LVDS capture mode: the DCA1000EVM user guide. Never change switches with power on.

## 3 Flash (SOP0 + SOP2 closed)

Confirmed on one IWR1843BOOST: the 2026-10-06 image flashed and booted in SOP 001 to `mmwDemo:/>`; the current image (prompt
`mm_sar_lvds:/>`) on 2026-10-10; the stock-demo restore on 2026-10-07 and 2026-10-10. Route notes:
[`docs/research/iwr1843_headless_flash_2026-10-06.md`](https://github.com/davidmhunt/CPSL_TI_Radar/blob/main/docs/research/iwr1843_headless_flash_2026-10-06.md).

1. SOP switch S1 (SOP2, SOP1, SOP0 left to right, ON = up; `readme_images/IWR1843_SOP_nodes.png`, repository root): flash mode
   is SOP2 and SOP0 ON, SOP1 OFF (101). Close every terminal and viewer on the port.
2. **Power-cycle fully (unplug USB and the 5 V supply, replug both) before EVERY attempt, retries included.** A retry without it
   failed with `XXXX Received unexpected data!!!XXXX` / `Not able to connect to serial port`.
3. Dry run first: `./fw flash iwr1843_sar_lvds /dev/serial/by-id/<...XDS110...-if00> --dry-run` (prints the command and image sha256;
   compare with section 1). Then the same without `--dry-run`, in your own terminal; type `FLASH MODE CONFIRMED` when asked (it
   refuses without a TTY; scripted flashing: [`../../../docs/reference/fw_cli.md`](../../../docs/reference/fw_cli.md)).
4. Success: `SUCCESS!! File type META_IMAGE1 downloaded successfully to SFLASH.` (after `Erase storage completed successfully!`).
   Observed: exit code 0 (`Flashed (DSLite rc=0)`) and no trailing `Can't Run Target CPU` line. On failure: keep `build/flash_output.log`,
   power-cycle fully, retry once, then stop and report.
5. Restore the stock demo (also the recovery; the flash formats all SFLASH): power-cycle in SOP 101, then `./fw build ti_stock_demos` and
   `./fw flash ti_stock_demos <port>` (default image `projects/ti_stock_demos/build/iwr1843_demo.bin`; same gates). Confirmed on the
   bench 2026-10-07: rc=0, `mmwDemo:/>` in SOP 001; stock image sha256 cd43e0dfccc8a96a9ffc571ea36a365830eaba7951000892f02078fd2bdac559.
6. Fallback only if `./fw flash` fails repeatedly: UniFlash GUI "Generate Package" (device IWR1843, Meta Image 1 = the image, COM port).
7. Power off and set SOP2 OFF (SOP0 stays ON, 001), then power-cycle (always, even when DSLite reports success). Success is confirmed only in section 4 (the `mm_sar_lvds:/>` prompt).

## 4 Run mode (SOP0 only)

1. Power on with only SOP0 ON (SOP[2:0] = 001, functional mode).
2. Open the CLI port at 115200 baud (for example `picocom -b 115200 /dev/serial/by-id/...-if00`). Press Enter: the prompt
   `mm_sar_lvds:/>` appears. If nothing appears: check the by-id port, that SOP2 is really OFF, and power-cycle once.

## 5 First bring-up check

This firmware accepts a cfg re-send without a power cycle (`sensorStop`, `flushCfg`, cfg; observed 2026-10-10); only `channelCfg`/`adcCfg`/`lowPower` changes need one.
`bench_check.py` finds the CLI port itself (the single `*XDS110*-if00`; `--cli-port` overrides) and prints each reply plus PASS/FAIL:
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
      The checks: [`tuning_guide.md`](tuning_guide.md) section 5. On `REJECTED`, keep both files and report the failed check.
   No datagrams at all: recheck section 2 and the DCA1000 switches.
5. The tool ends the run with `sensorStop`; `sarStats` counters reset at every `sensorStart`, so they are from the capture run.

## 6 Common runs

After section 5 passed, board in run mode (no power cycle between these). `./bench <name>` runs one; each ends with
`RESULT: PASS|FAIL` and a short block to paste back; captures and cfgs go to `/tmp/bench_run/`. Source: `tools/bench_run.py`.

| Command | What it checks |
|---|---|
| `./bench long` | 60 s capture: not cut off by `--timer-s 30`, bytes = `chirpAvail` x B, 0 UDP sequence gaps (CONFIG_PACKET_DATA delay unit) |
| `./bench restart` | 4 cycles of sensorStop, flushCfg + cfg with changed rxGain/HPF (channelCfg, lowPower, adcCfg identical), sensorStart, 30 s capture, parse |
| `./bench chan` | full cfg with `channelCfg` changed (every line answers Done: the CLI only stores it), then `sensorStart`: expect `Error: channelCfg differs from the first sensorStart`, Sensor State not 2, no Exception; then the original cfg + capture still works |
| `./bench finite` | `numFrames 5`: run ends by itself, `sensorStop` still works, `chirpAvail` = 5 x 255, `LVDS HW frames done` = 5 |
| `./bench start0` | `sensorStart 0` restart with no new cfg, capture + parse |
| `./bench adc` | gain 48 capture (reflector close, clipping): peak I and Q counts and the `--adc-bits` it implies (12 or 16); `--capture FILE` re-analyses an old capture |

## 7 No-reflector runs (about 10 min, no physical action)

Source: `tools/bench_a.py`, analysis in `tools/bench_stream.py`. Run in the order below (board in run mode, DCA1000 up); each
re-sends its own cfg. Stop and report if one FAILs. Power-cycle only if a command left the CLI dead (`fmt4` is last for that reason).

| Command | What it checks | min |
|---|---|---|
| `./bench bytes` | raw byte order (HSI id `DC 0A DC 0A DA 0C DA 0C` at packet start, `"SA" 01 00 "RM"` at M and M+32, unscramble gives `SARM`, every slot k mod 2 validates) and the other-slot regime (k+1 or k-1, >= 1000 packets, last packet of each frame excluded). `--capture FILE` re-analyses an old capture with no board | 1 |
| `./bench irq` | `sarStats` after `sensorStop`, 2 reads 4 s apart, one more with the cfg re-sent (before the next `sensorStart`): `chirpStartIsr` = `chirpAvail` = `chirps`, nothing moves | 1 |
| `./bench bsize` | B for header on (13328), header off (13264), and Ns 3302 (R*Ns = 2 mod 4: H 56, B 13328; cfg-checked), 10 s each | 2 |
| `./bench late` | a good capture, then offline copies (first packet cut and renumbered; one datagram dropped), then a capture armed 2 s after `sensorStart` | 2 |
| `./bench fmt1` | dataFmt 1 (`-1 1 1 0`) 30 s: 0 gaps, stock frame size, offline decode with the C++ converter's pairing, LVDS frame count, `sarStats`, then back to dataFmt 2 | 2 |
| `./bench fmt4` | dataFmt 4 with CQ monitors on (`analogMonitor 1 1`, `CQSigImgMonitor 0 111 4`), then off, then back to dataFmt 2; sizes, 0 gaps, no frame-end warning, LVDS frame count. dataFmt 4's block contents are NOT parsed (no parser path) | 2 |
| `./bench soak` | NO reflector: the 30 s pre-check, then 600 s of the example cfg as shipped (default gain/HPF, `analogMonitor`/`CQRxSatMonitor` on), `sarStats` each minute, post-stop reads. Needs >= 8 GB free in `/tmp/bench_run/` (else `RESULT: ABORTED`, nothing sent). Saturation counters are recorded, not judged; reflector rows print `NOT EVALUATED (no reflector)`. Writes `/tmp/bench_run/soak_summary.json` + `soak_summary.txt` (<= 40-line paste block). ~4 GB: delete with `rm -f /tmp/bench_run/soak.cap /tmp/bench_run/soak.cap.sarstats.json /tmp/bench_run/endurance_pre*` | 13 |

Order: `./bench bytes --capture /tmp/bench_run/long.cap` (offline: the 60 s capture from `./bench long`), then the table top to bottom. If `fmt4` shows frame or `sensorStart` errors with CQ on, rerun with
`--tb-add-us 500` (the signal/image monitor adds time to the blank) and report both.

## 8 Reflector runs (a corner reflector at a measured range)

Not yet bench-verified: phase continuity, tuning, boundary Tb. Source: `tools/bench_b.py`; each command prints one `RESULT` line per
goal G1-G7 where it applies, then `RESULT: PASS|FAIL`. Physical: a static corner reflector (no wind, nobody moving near the beam)
and a tape measure; M is the boresight range in metres. The 10 min capture is about 4 GB in `/tmp/bench_run/`: delete it afterwards.

| Command | What it does | min |
|---|---|---|
| `./bench adc` | (first) writes `/tmp/bench_run/adc_bits.txt` once it decides 12 or 16 bit; the others read it, or take `--adc-bits N` | 1 |
| `./bench tune --range M` | Gain 24/30/36/42 x HPF 175:350 and 350:700, 5 s each (`--gains`, `--hpf`, `--duration` change it); table of ADC-clipped chirps, firmware saturation flags, noise, reflector SNR; prints the candidates (0 clipped) and the chosen point with the next command | 3 |
| `./bench sat --range M --gain G --hpf A:B` | >= 5 gains from the tuned G up (steps of 4) to clipping, 30 s each; firmware `saturatedChirps` vs parser vs ADC-clipped, non-decreasing, 0 at G, > 0 at the top, lag alignment vs no lag | 5 |
| `./bench endurance --range M --gain G --hpf A:B` | 30 s cross-check through `sar_parse` + `sar_tune_report`, then 600 s with `sarStats` each minute, `sensorStop`, 2 more reads and one before the next start; streaming analysis (the 4 GB capture does not fit `sar_parse`'s in-memory design) | 15 |
| `./bench tb --add-us N --range M --gain G --hpf A:B` | Follow-up (only if endurance reports a step): 60 s with Tb = 300 + N us (N 100..500), same step statistics | 2 |

Order: (1) reflector at 1-2 m, close enough to clip at the default 48 dB (`./bench adc --gain N` changes it): `./bench adc` (prints
`saved ... --adc-bits N`), `./bench tune --range 1.5` (use the real range; note the CHOSEN gain G and HPF A:B), `./bench sat --range 1.5
--gain G --hpf A:B` (the top gain must clip: move the reflector closer if it does not). (2) Move the reflector to a measured 3-10 m and
leave it for 20 minutes: `./bench endurance --range M --gain G --hpf A:B`. (3) Only if the output says "G6 follow-up" (steps at
Tb = 300 us): `./bench tb --add-us 100 ...`, then 200 up to 500 until clean. Add `--adc-bits N` to each if `./bench adc` was not run.

## 9 Hand-off: what to record

Write down: the by-id ports, the host and DCA1000 addresses used, the image sha256 flashed, the flash method and output, each
check's result, the `sarStats` and `chirpAvail` numbers, and anything unexpected. Power down, restore the switches as found, and
tell whoever tracks bench use the boards are free.

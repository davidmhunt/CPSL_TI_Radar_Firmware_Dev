# Bench bring-up: flash the IWR1843BOOST and run the first check

Numbered steps for the person at the bench. Each step says what you should see and what to do if you do not. Steps marked
"(untested)" have not been confirmed on hardware yet. Paths are relative to `firmware_dev/` unless noted.

## 1 Before you start

Bring: the IWR1843BOOST, its 5 V supply, a micro-USB cable, the DCA1000EVM with its own 5 V supply, the cable or mounting
that joins the two boards for LVDS (per the DCA1000 guide), an Ethernet cable from the DCA1000 to the host, and two spare jumpers.

- Image: `projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin`, 152132 bytes, sha256
  `53948f4d20ca501490dd3d1dbe48229622af5346914dfe33daec33980864267a` (built 2026-10-05T23:51Z from `firmware_dev` commit 946f48d).
  Check: `sha256sum projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin`. A different hash means a different build: stop and ask.
- Host: your user must be in the `dialout` group (`id -nG | grep dialout`; if missing, add it and log in again).
- Ports, once the board is powered and plugged in: `ls /dev/serial/by-id/`. The XDS110 shows two serial ports:
  `...XDS110...-if00` is the CLI port (115200 baud, used here); `...-if03` is the data port (unused by this firmware).
  The `/dev/ttyACM*` numbers can change after a power cycle; the by-id names do not.

## 2 Host network (DCA1000)

The DCA1000 uses its factory addresses: FPGA `192.168.33.180`, host NIC `192.168.33.30/24`, command port 4096, data port 4098.
The tools in `projects/iwr1843_sar_lvds/tools/` default to exactly these. (The C++ driver's own configs use other values; that
does not matter here.)

1. Connect the DCA1000 to the host Ethernet port and power it with its 5 V supply. The FPGA/power LEDs light.
2. Give the host NIC the address: `ip addr` should list `192.168.33.30/24` on the wired interface cabled to the DCA1000. If not,
   add it (NetworkManager profile or `sudo ip addr add 192.168.33.30/24 dev <nic>`); `CPSL_TI_Radar_cpp/Readme.md`
   (repository root) describes the host prerequisites, including the UDP receive-buffer limit for high data rates.
3. `ping -c 3 192.168.33.180` answers. If not: check the cable and link LED, the DCA1000 power, and that the NIC has the
   address above.
4. DCA1000 mode switches: set them for the LVDS capture mode with the board powered from its own supply, per the DCA1000EVM
   user guide (TI SPRUIJ4) section on switch settings. Do not change switches with power on.

## 3 Flash (SOP0 + SOP2 closed)

1. Power off. Close SOP0 and SOP2 (flashing mode). Power on and connect USB.
2. The flash method and exact commands are not decided yet.

> **PLACEHOLDER: the flash command, UniFlash settings, success text and the stock-demo restore procedure go here once the flash
> route is chosen. Do not improvise a flash command.**

## 4 Run mode (SOP0 only)

1. Power off. Remove the SOP2 jumper so only SOP0 is closed (functional mode). Power on.
2. Open the CLI port at 115200 baud (for example `picocom -b 115200 /dev/serial/by-id/...-if00`). Press Enter: the prompt
   `mmwDemo:/>` appears (the boot banner names this firmware). If nothing appears: check the by-id port, that SOP2 is
   really open, and power-cycle once. Close the terminal before the next step; only one program may hold the port.

## 5 First bring-up check

A cfg is accepted once per power-up: power-cycle the board before this step if it has already been configured since power-on.
Run from `firmware_dev/`. Let `CLI=/dev/serial/by-id/...-if00` and `CFG=projects/iwr1843_sar_lvds/configs/sar_example_2ms.cfg`.

1. Send the cfg line by line and print every reply (untested on hardware):
   ```bash
   uv run python - <<'EOF'
   import sys; sys.path.insert(0, "projects/iwr1843_sar_lvds/tools")
   import sar_common as c
   p = c.CliPort("CLI")            # replace CLI with the by-id path
   for l in open("CFG"):           # replace CFG with the cfg path
       l = l.strip()
       if l and not l.startswith("%"):
           print(l, "->", p.command(l, timeout=5.0).strip().splitlines()[-1:])
   EOF
   ```
   Every line must end with `Done`; the last line is `sensorStart`. Any `Error` line: stop, keep the output, and report it.
2. In a terminal on the CLI port: `queryDemoStatus` reports the sensor running; `sarStats` shows chirps and frames increasing
   when sent twice a few seconds apart, with `chirpStartIsr` equal to `chirps`.
3. `sensorStop`. It must not print `no BSS frame-end event after sensorStop`.
4. Capture, which is also the reconfigure test: no power cycle since step 1. The capture tool sends `sensorStart` itself, so
   send the cfg without its last line (`sed '$d' CFG > /tmp/sar_nostart.cfg`, then step 1 with that file; it begins with
   `sensorStop` and `flushCfg`). Then:
   ```bash
   T=projects/iwr1843_sar_lvds/tools
   uv run python $T/dca_capture.py /tmp/bringup.cap --cli-port CLI --duration 5
   uv run python $T/sar_parse.py /tmp/bringup.cap --cfg CFG
   ```
   Pass: every line of the second cfg is acked, the parser prints checks 1 to 4 as passing and `VERDICT: ACCEPTED`, and
   `chirpAvail` in `/tmp/bringup.cap.sarstats.json` is about 2500 (5 s at 2 ms per chirp). Meaning of each check:
   `docs/tuning_guide.md` section 5. On `REJECTED`, keep both files and report which check failed. No datagrams at all:
   recheck section 2 and the DCA1000 switches. The capture options (`--fpga-ip --host-ip --cmd-port --data-port`) default
   to the factory addresses.
5. The tool ends the run with `sensorStop`. Confirm with `sarStats` that the counters are from the capture run only (it resets
   at every `sensorStart`).

## 6 Hand-off: what to record

Write down: the by-id port names, the host and DCA1000 addresses and ports actually used, the image sha256 flashed, the exact
flash method and its output, each check's result, the `sarStats` and `chirpAvail` numbers, and anything unexpected (extra
prompts, LED states, error text). Power the board down, put the jumpers back as found, and tell whoever tracks bench
use that the board and DCA1000 are free.

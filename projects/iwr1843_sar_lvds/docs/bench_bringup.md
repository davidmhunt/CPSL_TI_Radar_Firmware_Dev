# Bench bring-up: flash the IWR1843BOOST and run the first check

Numbered steps for the person at the bench, with what you should see and what to do if you do not. Steps marked
"(untested)" are unconfirmed on hardware. Paths are relative to `firmware_dev/` unless noted.

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
3. `ping -c 3 192.168.33.180` answers. If not: check the cable and link LED, the DCA1000 power, and that the NIC has the
   address above.
4. DCA1000 switches: for the network address, SW2.6 must be at position 11 (default FPGA addresses; position 6 loads
   whatever is saved in the EEPROM), per `DCA_Programming/README.md` (repository root). For the LVDS capture mode, see the
   DCA1000EVM user guide. Do not change switches with power on.

## 3 Flash (SOP0 + SOP2 closed)

Route and evidence: [`docs/research/iwr1843_headless_flash_2026-10-06.md`](../../../../docs/research/iwr1843_headless_flash_2026-10-06.md)
(repository root). Nothing here has been run on this board, so every command in this section is untested; the memo's
HYPOTHESIS labels apply to each point marked so below.

1. Install UniFlash 9.6.0 (none is installed yet). Download, no login, 382687118 bytes (365 MiB):
   `curl -L -O -C - https://software-dl.ti.com/ccs/esd/uniflash/uniflash_sl.9.6.0.5764.run`, then `chmod +x` it and run
   `./uniflash_sl.9.6.0.5764.run --help`. HYPOTHESIS: it accepts `--mode unattended --prefix <dir>`, and `dslite.sh` then
   sits in that prefix (`find <dir> -name dslite.sh`). Skip if you use the fallback below.
2. Close the board and the port. The SOP lines are the S1 switch block on the IWR1843BOOST, labelled SOP2, SOP1, SOP0 left to
   right (ON = up; diagram: `readme_images/IWR1843_SOP_nodes.png`, repository root). Flash mode is SOP2 and SOP0 ON, SOP1
   OFF (101). Power off, set that, power on, then power-cycle once more before UniFlash starts. Nothing else may hold the CLI port (close terminals and viewers; `lsof "$(readlink -f $CLI)"` should
   print nothing; `CLI` is defined in section 5).
3. Flash `projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin` as Meta Image 1 over the XDS110 `...-if00` port (the
   Application/User UART, as in section 1). Three parts:
   - A ccxml file for the IWR1843 serial connection. HYPOTHESIS: it uses `connections/Serial_Connection.xml` and
     `devices/iwr1843.xml` from UniFlash's targetdb (the logged AWR1843 run used `devices/awr1843.xml`). Writing it by hand is
     unproven: if `dslite.sh` rejects it, use the fallback.
   - List the setting ids first (the 9.6 spellings are not documented): `dslite.sh flash -c <ccxml> -S '.*'`, and the
     operations with `-p`. Use the one that sets the COM port (the memo's log used `COMPort`) and the one for format on download.
   - Flash: `dslite.sh flash -c <ccxml> -s COMPort=/dev/serial/by-id/<...-if00> -e -g flash.log <image>`, with the image given
     as Meta Image 1. The 5.1 log wrote it `-f file,1`; 9.6 lists files at the end and its Meta Image 1 syntax is not
     documented (untested), so take it from `-p` or the fallback package.
4. Expected output (from the logged AWR1843 run, older UniFlash on Windows): `Connecting to COM Port ...`, `Set break signal`,
   `AWR1843 device, fileType=META_IMAGE1 detected -> OK`, `Formatting SFLASH storage...`, `Erase storage completed successfully!`,
   `Downloading [META_IMAGE1] size [...]` (your image: 152132), then `SUCCESS!! File type META_IMAGE1 downloaded successfully to SFLASH.`
   A later `error: Cortex_R4_0: Can't Run Target CPU: Unsupported GTI Function.` was harmless in that log; accept it only after
   the SUCCESS line, and note the exit code. On any other error: keep `flash.log`, power-cycle, re-plug USB, kill any stale
   process from `pgrep -a -i 'dslite|python'` that holds the port, retry once, then stop and report.
5. Restore the stock demo (also the recovery if the flash fails; the flash formats all of SFLASH): repeat steps 2 and 3 with `projects/ti_stock_demos/build/iwr1843_demo.bin` (324804 bytes) as Meta Image 1.
6. Fallback, if the install or the hand-written ccxml fails: on any machine with the UniFlash GUI, use "Generate Package" (device
   IWR1843, Meta Image 1 = the image above, the COM port, format option). It emits a zip with `dslite` and a script;
   copy it over, edit the COM port to the by-id path and run it.
7. Power off and set SOP2 OFF (SOP0 stays ON). Success is confirmed only in section 4 (the `mmwDemo:/>` prompt).

## 4 Run mode (SOP0 only)

1. Power on with only SOP0 ON (SOP[2:0] = 001, functional mode).
2. Open the CLI port at 115200 baud (for example `picocom -b 115200 /dev/serial/by-id/...-if00`). Press Enter: the prompt
   `mmwDemo:/>` appears. If nothing appears: check the by-id port, that SOP2 is really OFF, and power-cycle once.

## 5 First bring-up check

A cfg is accepted once per power-up: power-cycle the board before this step if it has already been configured since power-on.
Run from `firmware_dev/` in one shell, after defining:
```bash
CLI=$(ls /dev/serial/by-id/*XDS110*-if00)
CFG=projects/iwr1843_sar_lvds/configs/sar_example_2ms.cfg
```

1. Send the cfg line by line and print every reply (untested on hardware):
   ```bash
   uv run python - "$CLI" "$CFG" <<'EOF'
   import sys; sys.path.insert(0, "projects/iwr1843_sar_lvds/tools")
   import sar_common as c
   p = c.CliPort(sys.argv[1])
   for l in open(sys.argv[2]):
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
   send the cfg without its last line (`sed '$d' "$CFG" > /tmp/sar_nostart.cfg`, then step 1 with that file; it begins with
   `sensorStop` and `flushCfg`). Then:
   ```bash
   T=projects/iwr1843_sar_lvds/tools
   uv run python $T/dca_capture.py /tmp/bringup.cap --cli-port "$CLI" --duration 5
   uv run python $T/sar_parse.py /tmp/bringup.cap --cfg "$CFG"
   ```
   Pass: every line of the second cfg is acked, the parser prints checks 1 to 4 as passing and `VERDICT: ACCEPTED`, and
   `chirpAvail` in `/tmp/bringup.cap.sarstats.json` is 2295 or 2550 (9 or 10 frames of 255 chirps at 510.3 ms; record it).
   Meaning of each check: `docs/tuning_guide.md` section 5. On `REJECTED`, keep both files and report the failed check.
   No datagrams at all: recheck section 2 and the DCA1000 switches. The capture options (`--fpga-ip --host-ip --cmd-port
   --data-port`) default to the factory addresses.
5. The tool ends the run with `sensorStop`; `sarStats` counters reset at every `sensorStart`, so they are from the capture run.

## 6 Hand-off: what to record

Write down: the by-id port names, the host and DCA1000 addresses and ports used, the image sha256 flashed, the exact flash
method and output, each check's result, the `sarStats` and `chirpAvail` numbers, and anything unexpected (extra prompts, LED
states, error text). Power down, restore the switches as found, and tell whoever tracks bench use the boards are free.

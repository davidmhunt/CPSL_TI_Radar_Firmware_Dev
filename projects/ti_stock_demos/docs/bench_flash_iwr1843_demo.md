# Bench: flash the stock IWR1843 demo and check the CLI

Status: **untested** on hardware (firmware-19 Step 2 pending); the identical flow is bench-confirmed for `iwr1843_sar_lvds`
([`bench_bringup.md`](../../iwr1843_sar_lvds/docs/bench_bringup.md) section 3). No cfg is sent here. Paths are relative to `firmware_dev/`.

## 1 Prerequisites

- IWR1843BOOST, micro-USB cable, its 5 V supply. Nothing else (no DCA1000 needed).
- Image `projects/ti_stock_demos/build/iwr1843_demo.bin`, 324804 B, sha256
  `cd43e0dfccc8a96a9ffc571ea36a365830eaba7951000892f02078fd2bdac559` (check with `sha256sum`; a different hash: stop and ask).
- You are in the `dialout` group; `picocom` installed (`command -v picocom`), or use the miniterm fallback in step 6.
- The board is yours (the gui loop holds the claim on all radars); close every terminal/viewer on the port.

## 2 Flash mode (SOP 101)

Power off (unplug USB and 5 V). Switch S1 (SOP2, SOP1, SOP0 left to right, ON = up): SOP2 ON, SOP1 OFF, SOP0 ON.
Then **power-cycle fully (plug USB and 5 V)** before EVERY attempt, retries included; a retry without it fails with
`Received unexpected data`.

## 3 Dry run, then flash

Run in your own terminal (the flash needs a TTY and asks you to type `FLASH MODE CONFIRMED`):

```bash
cd ~/Documents/radar_dev/CPSL_TI_Radar/firmware_dev
PORT=$(ls /dev/serial/by-id/*XDS110*-if00)   # CLI port; expect exactly one line
echo "$PORT"
sha256sum projects/ti_stock_demos/build/iwr1843_demo.bin
./fw flash ti_stock_demos "$PORT" --dry-run   # exit 0, prints the DSLite command and sha256
./fw flash ti_stock_demos "$PORT"             # real flash
```

Success: `SUCCESS!! File type META_IMAGE1 downloaded successfully to SFLASH.` then
`Flashed (DSLite rc=0)`. Output is also in `projects/ti_stock_demos/build/flash_output.log`.

## 4 If it fails

Keep `build/flash_output.log`, power-cycle fully (step 2), retry once. Still failing: stop and report the log.

## 5 Functional mode (SOP 001)

Power off, set SOP2 OFF (SOP0 stays ON, SOP1 OFF), power on.

## 6 CLI check

```bash
picocom -b 115200 "$PORT"        # exit: Ctrl-A Ctrl-X
# fallback: uv run python -m serial.tools.miniterm "$PORT" 115200   (exit: Ctrl-])
```

Press Enter: expect the `mmwDemo:/>` prompt. Type `version`: expect a reply from the SDK 3.6 demo (platform xWR18xx,
SDK 03.06.02.00 line; paste it as seen). Do not send a cfg (accepted once per power-up). Close the terminal.

## 7 Report back

DSLite rc (`Flashed (DSLite rc=...)`), the `SUCCESS!!` line, the `mmwDemo:/>` prompt, the `version` reply, and any failure
log. The Firmware role then replaces "untested" in the README and `bench_bringup.md`.

## 8 Restore the SAR image afterwards

SOP 101, full power-cycle, then:

```bash
./fw flash iwr1843_sar_lvds "$PORT" --dry-run && ./fw flash iwr1843_sar_lvds "$PORT"
```

(then SOP 001 and power-cycle, as in `bench_bringup.md` section 3).

## 9 Hand-off

After the report the board goes to the GUI loop (SOP 001, powered, demo on the CLI port); release the claim in `status.md`.

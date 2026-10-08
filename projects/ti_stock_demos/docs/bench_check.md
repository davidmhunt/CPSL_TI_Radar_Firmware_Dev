# ti_stock_demos bench check (IWR1843 stock demo)

Run by a person at the bench; not run by `./fw test`. Record the outcome in `project.toml` as a `[[bench]]` entry.
No cfg is sent. Paths are relative to `firmware_dev/`. The IWR6843 image has no headless flasher and no bench record.

## Preconditions

- IWR1843BOOST, micro-USB cable, its 5 V supply. Nothing else (no DCA1000 needed).
- Image `projects/ti_stock_demos/build/iwr1843_demo.bin` from `./fw build ti_stock_demos --variant 18xx`, 324804 B, sha256
  `cd43e0dfccc8a96a9ffc571ea36a365830eaba7951000892f02078fd2bdac559` (check with `sha256sum`; a different hash: stop and ask).
- You are in the `dialout` group; `picocom` installed (`command -v picocom`), or use the miniterm fallback below.
- The board is yours (claim it in `status.md`); close every terminal/viewer on the port.

## Board state

- Flash mode (SOP 101): power off (unplug USB and 5 V). Switch S1 (SOP2, SOP1, SOP0 left to right, ON = up): SOP2 ON, SOP1 OFF,
  SOP0 ON. Then **power-cycle fully (plug USB and 5 V)** before EVERY attempt, retries included; a retry without it fails with
  `Received unexpected data`.
- Functional mode (SOP 001) for the CLI check: power off, SOP2 OFF (SOP0 stays ON, SOP1 OFF), power on.
- A cfg is accepted once per power-up; this check sends none.

## Steps

1. Dry run, then flash, in your own terminal (the flash needs a TTY and asks you to type `FLASH MODE CONFIRMED`):

   ```bash
   cd ~/Documents/radar_dev/CPSL_TI_Radar/firmware_dev
   PORT=$(ls /dev/serial/by-id/*XDS110*-if00)   # CLI port; expect exactly one line
   sha256sum projects/ti_stock_demos/build/iwr1843_demo.bin
   ./fw flash ti_stock_demos "$PORT" --dry-run   # exit 0, prints the DSLite command and sha256
   ./fw flash ti_stock_demos "$PORT"             # real flash
   ```

2. If it fails: keep `projects/ti_stock_demos/build/flash_output.log`, power-cycle fully, retry once. Still failing: stop and report the log.
3. Set functional mode (SOP 001), power on, then either `./fw verify ti_stock_demos --port "$PORT"` or by hand:

   ```bash
   picocom -b 115200 "$PORT"        # exit: Ctrl-A Ctrl-X
   # fallback: uv run python -m serial.tools.miniterm "$PORT" 115200   (exit: Ctrl-])
   ```

   Press Enter for the prompt, type `version`, then close the terminal.
4. Afterwards restore the SAR image if the board goes back to the capture work: SOP 101, full power-cycle, then
   `./fw flash iwr1843_sar_lvds "$PORT" --dry-run && ./fw flash iwr1843_sar_lvds "$PORT"` (SOP 001 and a power-cycle as in
   `projects/iwr1843_sar_lvds/docs/bench_bringup.md` section 3). Release the claim in `status.md`.

## Expected output

- Flash: `SUCCESS!! File type META_IMAGE1 downloaded successfully to SFLASH.` then `Flashed (DSLite rc=0)`; output also in
  `projects/ti_stock_demos/build/flash_output.log`.
- CLI, SOP 001, 115200: the `mmwDemo:/>` prompt. `version` reply (observed 2026-10-07): Platform xWR18xx; mmWave SDK 03.06.02.00;
  Device Info IWR18xx non-secure ES 02.00; RF F/W 02.00.00.01.17.10.05; RF patch 01.02.06.11.20.06.02; mmWaveLink 01.02.06.06;
  Lot/Wafer/X/Y are per chip (seen: 3937325, 2, 5, 34); then `Done`.

## Pass/fail

- Pass when the `SUCCESS!!` line, `rc=0`, the `mmwDemo:/>` prompt and the `version` reply above are all seen. Anything else is
  fail or partial (say which part). Report the DSLite rc, the `SUCCESS!!` line, the prompt, the `version` reply and any failure log.

## Results log

Results are recorded in `project.toml` `[[bench]]` (board, date, image sha256, result, this doc). Current record: IWR1843
flash plus `version`, pass (firmware-19 Step 2).

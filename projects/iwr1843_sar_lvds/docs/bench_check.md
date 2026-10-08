# iwr1843_sar_lvds bench check

Run by a person at the bench (IWR1843BOOST plus DCA1000); not run by `./fw test`. This is the short gated sequence: flash,
`fw verify`, one short capture. The full numbered procedure (host network, DCA1000 switches, every bench command, Set A and
Set B, hand-off notes) is [`bench_bringup.md`](bench_bringup.md); follow its sections rather than a copy here. Record the
outcome as a `[[bench]]` entry in `project.toml`. All commands run from `firmware_dev/`.

## Preconditions

- Board, its XDS110 ports and the DCA1000 free (claim them in `status.md`); your user in the `dialout` group; no terminal or viewer on the port.
- Image built with `./fw build iwr1843_sar_lvds`; `sha256sum projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin` matches the image you mean to test (a different hash is a different build: say so in the record).
- DCA1000 reachable at its factory addresses ([`bench_bringup.md`](bench_bringup.md) section 2). Use by-id ports (`ls /dev/serial/by-id/`); the CLI port ends in `-if00`.

## Board state

- Flash: SOP 101 (SOP2 and SOP0 ON, SOP1 OFF), set with power off, then a full USB + 5 V power-cycle. Repeat the power-cycle before EVERY flash attempt, retries included.
- Run: SOP 001 (SOP0 only), power-cycled. A cfg is accepted once per power-up for the cascade only; this firmware reconfigures after `sensorStop` + `flushCfg` without a power cycle, except `channelCfg`/`adcCfg`/`lowPower` changes.

## Steps

1. `./fw flash iwr1843_sar_lvds /dev/serial/by-id/<...XDS110...-if00> --dry-run`: prints the checklist, the DSLite command and the image sha256; compare it with the build.
2. Flash for real in your own terminal (type `FLASH MODE CONFIRMED` at the prompt), or script it with `--plan` then `--confirm <token>`.
3. Power off, SOP 001, power on. `./fw verify iwr1843_sar_lvds --port /dev/serial/by-id/<...-if00>` (the probe set is the descriptor's `identify` block; it sends no chirp cfg).
4. One short capture ([`bench_bringup.md`](bench_bringup.md) section 5): `uv run python projects/iwr1843_sar_lvds/tools/bench_check.py capture --duration 5`.
5. Optional longer runs (`./bench long`, Set A, the 10-minute soak) are in [`bench_bringup.md`](bench_bringup.md) section 5b and 5c; Set B (reflector) is section 5d.

## Expected output

- Flash: `SUCCESS!! File type META_IMAGE1 downloaded successfully to SFLASH.`, `Flashed (DSLite rc=0)`, no trailing `Can't Run Target CPU`.
- Run mode: the CLI answers at 115200 baud with the `mmwDemo:/>` prompt; `version` reports `Platform : xWR18xx`; `sarStats` answers `Done`.
- Capture: every cfg line acked `Done`, parser checks 1 to 4 pass and `VERDICT: ACCEPTED`; `chirpAvail` 2295 or 2550 for a 5 s capture of the example cfg.

## Pass/fail

- Pass when every expected output above is seen. `Received unexpected data` on flash means the power-cycle was skipped: power-cycle fully and retry once. Anything else is fail or partial; say which part and keep `build/flash_output.log` and the capture files.
- The prompt text and the `sarStats` reply are as of today's image; firmware-17 changes them and updates the descriptor and this section.

## Results log

Results are recorded only in `project.toml` `[[bench]]` (board, date, image sha256, result, doc link): never in this file.

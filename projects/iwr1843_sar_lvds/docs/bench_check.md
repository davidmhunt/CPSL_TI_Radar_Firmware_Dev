# iwr1843_sar_lvds bench check

A person runs this at the bench (IWR1843BOOST plus DCA1000); `./fw test` does not. It is the short sequence: flash, `fw verify`, one
short capture. The full procedure (host network, DCA1000 switches, every bench command) is [`bench_bringup.md`](bench_bringup.md).
All commands run from `firmware_dev/`.

## Preconditions

- Board, its XDS110 ports and the DCA1000 free (claim them in `status.md`); your user in the `dialout` group; no terminal or viewer on the port.
- Image built with `./fw build iwr1843_sar_lvds`; `sha256sum projects/iwr1843_sar_lvds/build/iwr1843_sar_lvds.bin` matches the image you mean to test (a different hash is a different build: say so in the record).
- DCA1000 reachable at its factory addresses ([`bench_bringup.md`](bench_bringup.md) section 2). Use by-id ports (`ls /dev/serial/by-id/`); the CLI port ends in `-if00`.

## Board state

- Flash: SOP 101 (SOP2 and SOP0 ON, SOP1 OFF), set with power off, then a full USB + 5 V power-cycle. Repeat the power-cycle before EVERY flash attempt, retries included.
- Run: SOP 001 (SOP0 only), power-cycled. This firmware accepts a cfg re-send (`sensorStop` + `flushCfg` + cfg) without a power cycle; only `channelCfg`/`adcCfg`/`lowPower` changes need one.

## Steps

1. `./fw flash iwr1843_sar_lvds /dev/serial/by-id/<...XDS110...-if00> --dry-run`: prints the checklist, the DSLite command and the image sha256 (no port touched); compare it with the build.
2. Flash for real in your own terminal (type `FLASH MODE CONFIRMED` at the prompt).
3. Power off, SOP 001, power on. `./fw verify iwr1843_sar_lvds --port /dev/serial/by-id/<...-if00>` (sends `version` and `sarStats`, no chirp cfg).
4. One short capture ([`bench_bringup.md`](bench_bringup.md) section 5): `uv run python projects/iwr1843_sar_lvds/tools/bench_check.py capture --duration 5`. Add `--cli-port /dev/serial/by-id/<...XDS110...-if00>` when two XDS110 boards are attached (required then; the default needs exactly one).
5. Optional longer runs (`./bench long`, the no-reflector runs and 10-minute soak, the reflector runs) are in [`bench_bringup.md`](bench_bringup.md) sections 6 to 8.

## Expected output

- Flash: `SUCCESS!! File type META_IMAGE1 downloaded successfully to SFLASH.`, `Flashed (DSLite rc=0)`, no trailing `Can't Run Target CPU`.
- Run mode: the CLI answers at 115200 baud with the `mm_sar_lvds:/>` prompt; `version` reports `Platform : xWR18xx`; `sarStats` answers `Done`.
- Capture: every cfg line acked `Done`, parser checks 1 to 4 pass and `VERDICT: ACCEPTED`; `chirpAvail` 2295 or 2550 for a 5 s capture of the example cfg.

## Pass/fail

- Pass when every expected output above is seen. `Received unexpected data` on flash means the power-cycle was skipped: power-cycle fully and retry once. Anything else is fail or partial; say which part and keep `build/flash_output.log` and the capture files.
- A board printing `mmwDemo:/>` runs the old SAR image or the stock demo; the current image prints `mm_sar_lvds:/>`.

## Results log

Results are recorded only in `project.toml` `[[bench]]` (board, date, image sha256, result, doc link): never in this file.

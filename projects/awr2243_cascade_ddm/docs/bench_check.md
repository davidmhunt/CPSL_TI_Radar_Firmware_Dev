# awr2243_cascade_ddm bench check

Run by a person at the bench; not run by `./fw test`. Record the outcome in `project.toml` as a `[[bench]]` entry (board AWR2243_CASCADE, date, image sha256, result, this doc). The cascade takes one cfg per power-up: every configured run needs a fresh power-cycle.

## Preconditions

- AWR2243-2X-CAS-EVM on 12 V (USB only enumerates with 12 V on), XDS110 USB connected; claim the board and its ports in `status.md`.
- Image built: `./fw build awr2243_cascade_ddm`; note the `.appimage` sha256 from `build/build_info.json`.
- The by-id CLI port (`./fw ports awr2243_cascade_ddm`, ends in `-if00`) is free: no monitor, driver or ModemManager holds it.

## Board state

- Before flashing: J6 on the bottom two pins (UART flash mode), set with power off, then power-cycle (12 V off/on).
- After flashing: J6 on the top two pins (QSPI run mode), set with power off, then power-cycle.
- After every configured run: power-cycle again before the next cfg.

## Steps

1. J6 bottom, power-cycle. `./fw flash awr2243_cascade_ddm <by-id -if00 port> --dry-run`, check checklist and sha256, then `./fw flash awr2243_cascade_ddm <by-id -if00 port>` and type `FLASH MODE CONFIRMED`.
2. Power off, J6 top, power-cycle.
3. `./fw verify awr2243_cascade_ddm --port <by-id -if00 port>`: listen-only; reports `skipped` (the descriptor's `once_safe` is false) and sends nothing.
4. One configured run (uses the one cfg of this power-up): `docker compose run --rm flash python3 /build_context/projects/awr2243_cascade_ddm/tools/cascade_serial_check.py --cli <CLI> --data <DATA> --cfg /build_context/projects/awr2243_cascade_ddm/configs/cascade_shortrange.cfg` (or the driver with a cascade config). Power-cycle afterwards.

## Expected output

- Step 1: flasher prints `All commands from config file are executed`; `fw` exits 0.
- Step 3: outcome `skipped`, exit 0, no bytes written to the port.
- Step 4: every cfg command answers `Done`; TLV frames arrive on the data port (3,125,000 baud) at the cfg frame rate with no frame-number gaps and no framing errors.

## Pass/fail

- Pass when every expected output above is seen; anything else is fail or partial (say which part). A flash failure on a new board may be the QSPI Quad Enable bit (see README Known limits).

## Results log

Results are recorded in `project.toml` `[[bench]]` (board, date, image sha256, result, this doc). No record yet.

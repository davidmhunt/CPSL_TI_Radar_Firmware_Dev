# awr2243_cascade_ddm

## Purpose

AM273x + 2x AWR2243 (AWR2243-2X-CAS-EVM) 2-chip cascade radar demo with DDM (Doppler-division
multiplexing) processing, built headlessly with CCS from the TI Radar Toolbox lab source. Streams a
TLV point cloud over the data UART; the host driver configures it over the CLI UART.

## Status

Quote of `./fw list`: status `built` (a `build/build_info.json` exists); no `[[bench]]` record,
because this build has never been flashed through `fw` (bench check: fwstd-09).

- Prebuilt libraries (`.aer5f`/`.ae66`) are not in git here: `build.sh` takes them from the Radar
  Toolbox in the image. The ignore rules still allow committing rebuilt copies under `src/ti/**/lib/`.
- **Open item: drop TI binaries.** `prebuilt_binaries/` holds TI's SBL images (`sbl_qspi`,
  `sbl_uart_uniflash`, needed by `flash.sh`); whether TI binaries may be shipped
  publicly is an open licensing question (user decision).

## Build

From `firmware_dev/` (prerequisites: `projects/README.md`):

```bash
./fw build awr2243_cascade_ddm            # CCS headless, ~15 min; CCS_CONFIG=Debug for a debug build
```

Outputs in `build/`: `am273x_cascade.appimage` (flashable, both cores), `am273x_cascade.elf` (MSS),
`am273x_cascade_dss.xe66` (DSS), `build_info.json` (sha256 of each), `build_info.txt`; the CCS
workspace and its logs stay in `build/ccs_workspace/`. Binaries embed the in-container build path,
so hashes compare only at the same project folder name (`docs/firmware.md` Quirks).

## Test

`./fw test awr2243_cascade_ddm` (no hardware, no Docker): manifest and descriptor consistency, the
`configs/cascade_*.cfg` against the descriptor limits, and the `uart_uniflash.py` flush-patch anchor
check (runs only when the `firmware-env` image is present, otherwise skipped and said so).

## Flash

Method `uart_uniflash` (MCU+ SDK `uart_uniflash.py` through the SBL images), gate J6. From `firmware_dev/`:

```bash
./fw flash awr2243_cascade_ddm /dev/serial/by-id/<...>-if00 --dry-run   # checklist, command, sha256; flashes nothing
./fw flash awr2243_cascade_ddm /dev/serial/by-id/<...>-if00             # our build (TTY: J6 checklist + typed phrase)
./fw flash awr2243_cascade_ddm /dev/serial/by-id/<...>-if00 prebuilt    # TI's prebuilt appimage (TTY / dry run only)
```

Board setup: J6 on the bottom two pins = UART flash mode, top two pins = QSPI run mode; change only
with power off, and power-cycle (12 V) before flashing. After flashing, move J6 to the top two pins
and power-cycle. Success only on `All commands from config file are executed`. A chirp cfg is
accepted once per power-up.

## Verify

`./fw verify awr2243_cascade_ddm --port <by-id -if00 port>` is listen-only: the descriptor
(`cascade_ddm`, `identify.AWR2243_CASCADE`) has `once_safe` false, so no probe is sent and the outcome
is `skipped`. For a real bring-up check, `tools/cascade_serial_check.py` (manual, sends a cfg; see Bench check).

## Bench check

[docs/bench_check.md](docs/bench_check.md) (J6 flash, power-cycle, verify, one configured run). The
manual cfg + TLV check:
`docker compose run --rm flash python3 /build_context/projects/awr2243_cascade_ddm/tools/cascade_serial_check.py --cli <CLI> --data <DATA> --cfg /build_context/projects/awr2243_cascade_ddm/configs/cascade_shortrange.cfg`
(`--skip-config` to only listen).

## Layout

| Path | What |
|------|------|
| `project.toml` | Manifest read by `./fw` |
| `build.sh`, `flash.sh` | Container build and UART flash scripts (standard `FW_*` env) |
| `src/` | CCS projectspecs + demo and library sources (Radar Toolbox baseline plus our edits) |
| `configs/` | Chirp cfgs `cascade_*.cfg` |
| `prebuilt_binaries/` | TI SBL images for `flash.sh` |
| `tools/` | `cascade_serial_check.py` (manual bring-up check) |
| `tests/` | Hardware-free host tests |
| `docs/` | `cascade_demo_guide.md` (architecture, build, packet formats), `bench_check.md` |
| `build/` | Outputs (gitignored) |

## Changes vs TI

Baseline: Radar Toolbox 4.00.00.05, `source/ti/examples/Automotive_ADAS_and_Parking/mmwave_2_chip_cascade/src/awr2243`
(see `project.toml` `[source]`). `src/` is that tree plus our edits to the demo and library sources.
There is no unmodified-baseline commit: `704930d` ("track modified awr2243 firmware sources") is the
first commit that tracks the source, and it already includes the edits. To see what we changed,
diff `src/` against the Radar Toolbox install (`/opt/ti/radar_toolbox_4_00_00_05/...` in the image).
Also not from TI: `configs/`, `docs/cascade_demo_guide.md`, the build/flash scripts and `tools/`.

## TI references (not stored here)

- Two Chip Cascade user guide and release notes: in the Radar Toolbox download that `downloads/download.sh` fetches (`source/ti/examples/Automotive_ADAS_and_Parking/mmwave_2_chip_cascade/docs/`); toolbox page: https://www.ti.com/tool/download/RADAR-TOOLBOX
- AWR2243-2X-CAS-EVM user's guide (SWRU639): https://www.ti.com/lit/pdf/SWRU639

## Known limits

- One cfg per power-up (TI known issue): power-cycle before every configured run.
- `fw verify` cannot probe this firmware (it would count as the one cfg); it only listens.
- New boards may need the QSPI Quad Enable bit set (see "Possible Flashing Issues" in the cascade
  user guide; rebuild `sbl_uart_uniflash` with "Quad Enable Type" = 6).
- Not yet flashed or run through `fw` (bench check pending).

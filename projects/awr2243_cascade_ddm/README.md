# awr2243_cascade_ddm

AM273x + 2x AWR2243 (AWR2243-2X-CAS-EVM) 2-chip cascade radar demo with DDM (Doppler-division
multiplexing) processing, built headlessly with CCS from the TI Radar Toolbox lab source.

## What changed vs TI

Baseline: Radar Toolbox 4.00.00.05, `source/ti/examples/Automotive_ADAS_and_Parking/mmwave_2_chip_cascade/src/awr2243`
(see `project.env`). `src/` is that tree plus our edits to the demo and library sources.
There is no unmodified-baseline commit: `704930d` ("track modified awr2243 firmware sources") is the
first commit that tracks the source, and it already includes the edits. To see what we changed,
diff `src/` against the Radar Toolbox install (`/opt/ti/radar_toolbox_4_00_00_05/...` in the image).

Also not from TI: `configs/` holds the chirp cfgs (`cascade_*range*.cfg`), `docs/cascade_demo_guide.md`
is our developer guide, and the build/flash scripts are ours.

## Status

- Build: works, `./fw build awr2243_cascade_ddm` gives an appimage byte-identical to the pre-move
  build (firmware-02). Build is deterministic for the `.appimage` and `.elf`.
- On-board: not re-checked in firmware-02 (no board); `flash.sh` was only dry-run with a missing port.
- Prebuilt libraries (`.aer5f`/`.ae66`) are not in git here: `build.sh` takes them from the Radar
  Toolbox in the image. The ignore rules still allow committing rebuilt copies under `src/ti/**/lib/`.
- **Open item: drop TI binaries.** `prebuilt_binaries/` holds TI's SBL images (`sbl_qspi`,
  `sbl_uart_uniflash`, needed by `flash.sh`) and `demo.cfg`; whether TI binaries may be shipped
  publicly is an open licensing question (user decision). Nothing was dropped in the move.

## Build, flash, run

From `firmware_dev/` (prerequisites: `projects/README.md`):

```bash
./fw build awr2243_cascade_ddm                       # CCS_CONFIG=Debug for a debug build
./fw flash awr2243_cascade_ddm /dev/ttyUSB0 prebuilt # TI's prebuilt image: checks board and cables
./fw flash awr2243_cascade_ddm /dev/ttyUSB0          # our build
```

Outputs in `build/`: `am273x_cascade.appimage` (flashable, both cores), `am273x_cascade.elf` (MSS),
`am273x_cascade_dss.xe66` (DSS), `build_info.txt`; the CCS workspace and its logs stay in
`build/ccs_workspace/`.

Board setup: J6 on the bottom two pins = UART flash mode, top two pins = QSPI run mode; change only
with power off, and power-cycle before flashing. After flashing, move J6 to the top two pins and
power-cycle. A chirp cfg is accepted once per power-up. Bring-up check:
`docker compose run --rm flash python3 /build_context/tools/cascade_serial_check.py --cli <CLI> --data <DATA> --cfg /build_context/projects/awr2243_cascade_ddm/configs/cascade_shortrange.cfg`.

Details: [docs/cascade_demo_guide.md](docs/cascade_demo_guide.md) (architecture, build, packet formats).

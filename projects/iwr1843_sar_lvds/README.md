# iwr1843_sar_lvds

IWR1843BOOST mmw demo (TI mmWave SDK 3.6.02.00-LTS, `xwr18xx/mmw`) that will become the SAR /
LVDS raw-data firmware. Today it is the unmodified TI demo, built out of tree.

## What changed vs TI

Baseline: `project.env` (`BASELINE`, `BASELINE_COMMIT`). See every change since with
`git diff <BASELINE_COMMIT> -- projects/iwr1843_sar_lvds/src`.

- _(none yet: `src/` is the unmodified TI baseline)_

Copied verbatim from `packages/ti/demo/xwr18xx/mmw/` of the SDK: `mss/ dss/ include/ makefile
mmw_res.h` into `src/`, `profiles/*.cfg` into `configs/`, `profiles/mmwDemo_xwr18xx_update_config.pl`
into `tools/`. Left out (not source): `docs/` (generated doxygen) and TI's prebuilt outputs
(`*.bin *.map *.xer4f *.xe674 *.rov.xs`, including the AOP variants).

## LICENSE

The TI sources in `src/`, `configs/` and `tools/` are tracked under TI's own terms. The C sources and
headers carry TI's BSD-3-clause header ("Redistribution and use in source and binary forms ...").
Files without that header fall into two groups:

- **No header at all:** `makefile`, `*.mak`, `*_linker.cmd`, the `*.cfg` chirp profiles and the `.pl` helper.
- **Old header:** the two XDC/BIOS configs `mss/mmw_mss.cfg` and `dss/mmw_dss.cfg` carry an older
  "Copyright 2011 ... Restricted rights" boilerplate instead of the BSD text.

Both groups are covered by the SDK software manifest (`docs/mmwave_sdk_software_manifest.html`:
"mmwave drivers, control, datapath, utils & demo", `packages\ti\demo`, BSD-3-Clause). Keep these
headers when editing. No TI binaries are tracked; the
open release-gate item on shipping TI binaries publicly is unchanged by this project.

## Docs

- [`docs/lvds_code_map.md`](docs/lvds_code_map.md): where the demo streams ADC data over LVDS and where a custom payload hooks in.
- [`docs/sar_feasibility.md`](docs/sar_feasibility.md): IWR1843 / SDK 3.6 limits for 1TX/1RX continuous-chirp SAR, data-rate math, frame-boundary rule, go/no-go criteria.

## Status

**Scaffold.** `src/` is the unmodified TI demo. SAR mode planned; see `docs/sar_feasibility.md`.

- Build: `./fw build iwr1843_sar_lvds` passes (firmware-04, 2026-10-05); ARM CGT 16.9.6 and C6000 8.3.3
  ran; `/opt/ti` not modified (no file newer than a stamp taken before the build).
- Equivalence to the stock demo (`ti_stock_demos/build/iwr1843_demo.bin`, firmware-03 criterion): same
  `.bin` size (324804 B); MSS map memory configuration and segment allocation identical; DSS `.const.2`
  is 4 B larger because this path is 2 characters longer (`__FILE__` strings), which also lets the linker
  place `.bss` before `.far`. Built at a path of the same length as `ti_stock_demos`, both maps match
  exactly, and this project's `src/` and TI's SDK demo folder give byte-identical `.bin`s. Two builds
  here are byte-identical.
- On-board: not tested.

## How the build works

Same out-of-tree technique as `projects/ti_stock_demos` (see its README). `build.sh` deletes
`build/` and recreates an SDK overlay in `build/sdk/`: `packages/` is a symlink farm onto the SDK,
except `ti/demo/xwr18xx/mmw`, which is a fresh copy of this project's `src/` (TI prebuilt outputs
and `docs/` excluded). Every build is a clean build of the current `src/`, so local edits are
always picked up. After sourcing TI's `setenv.sh` it sets `MMWAVE_SDK_DEVICE=iwr18xx` and
`MMWAVE_SDK_INSTALL_PATH=build/sdk/packages`, then runs `make mmwDemo` (the non-AOP image).

Outputs in `build/`: `iwr1843_sar_lvds.bin` (flash image), `iwr1843_sar_lvds.elf` (MSS `.xer4f`),
`iwr1843_sar_lvds_dss.xe674`, `iwr1843_sar_lvds_{mss,dss}.map`, `build_info.txt`, `compilers.txt`.

## Build, flash, run

From `firmware_dev/` (see `projects/README.md` for prerequisites):

```bash
./fw build iwr1843_sar_lvds                 # outputs in projects/iwr1843_sar_lvds/build/
./fw flash iwr1843_sar_lvds /dev/ttyACM0    # prints the manual steps, exits 3
```

`flash.sh` has no headless flasher to call (no xWR18xx serial-flash target in the image's
DSLite; TI's tool is the UniFlash GUI), so it prints the steps: IWR1843BOOST SOP0+SOP2 closed
(flashing mode), power on, UniFlash with the CLI/UART port and `build/iwr1843_sar_lvds.bin` as
Meta Image 1, then SOP0 only (functional mode) and power-cycle. Send a `configs/*.cfg` over the
CLI port at 115200 baud; a cfg is accepted once per power-up.

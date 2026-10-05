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
The files without that header (`makefile`, `*.mak`, `*_linker.cmd`, `*.cfg` chirp profiles, the `.pl`
helper, and the two XDC/BIOS configs `mss/mmw_mss.cfg`, `dss/mmw_dss.cfg`, whose header is an older
"Copyright 2011 ... Restricted rights" boilerplate) are covered by the SDK software manifest
(`docs/mmwave_sdk_software_manifest.html`: "mmwave drivers, control, datapath, utils & demo",
`packages\ti\demo`, BSD-3-Clause). Keep these headers when editing. No TI binaries are tracked; the
open release-gate item on shipping TI binaries publicly is unchanged by this project.

## Status

- Build: not yet
- On-board: not yet

## Build, flash, run

From `firmware_dev/` (see `projects/README.md` for prerequisites):

```bash
./fw build iwr1843_sar_lvds                 # outputs in projects/iwr1843_sar_lvds/build/
./fw flash iwr1843_sar_lvds /dev/ttyACM0    # board-specific steps below
```

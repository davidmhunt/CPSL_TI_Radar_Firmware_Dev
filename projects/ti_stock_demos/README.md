# ti_stock_demos

TI's unmodified mmWave SDK 3.6.02.00-LTS `mmw` demos for the IWR1843 (`xwr18xx`) and the
IWR6843 (`xwr68xx`), built out of tree: nothing is written into `/opt/ti`.

## What changed vs TI

Nothing. This is a *stock* project: `src/` is empty (`.gitkeep`) and no TI source is tracked.
`BASELINE` in `project.env` names the SDK folders that are built
(`packages/ti/demo/xwr{18,68}xx/mmw`, inside the image at `/opt/ti/mmwave_sdk_03_06_02_00-LTS`).
`configs/` holds copies of TI's `profiles/*.cfg` (and the config-update `.pl` helper) per family. The TI `.cfg` and `.pl` files carry no license header; the SDK's
`docs/mmwave_sdk_software_manifest.html` lists them as BSD-3-Clause.

To modify a demo, start a new project (`./fw new`) and copy the demo folder into its `src/`.

## How the out-of-tree build works

TI's demo sources include `<ti/demo/xwr18xx/mmw/...>` and the makefiles write objects and images
in their own folder, so the demo has to sit at that path under an SDK root, and the SDK root
must not be `/opt/ti` (root-owned). `build.sh` therefore creates an overlay in `build/sdk/`:

- `build/sdk/packages/` is a symlink farm: every SDK `packages/*` entry links to the SDK, except
  the directories on the path to `ti/demo/xwr18xx/mmw` and `ti/demo/xwr68xx/mmw`, which are real
  directories. Those two `mmw` folders are real copies (without TI's prebuilt outputs and docs).
- `build/sdk/firmware` links to the SDK's `firmware/` (radar subsystem images).
- After sourcing TI's `setenv.sh` (from its own folder; it sets the device to `iwr68xx` by
  default), `build.sh` sets `MMWAVE_SDK_DEVICE` and points `MMWAVE_SDK_INSTALL_PATH` at the
  overlay, then runs `make mmwDemo` in the copied demo folder.
- Targets: `mmwDemo` for 18xx (standard image) and `all` for 68xx (which has no `mmwDemo`). TI's 18xx `all` also builds the AOP variants; those are not
  part of the old script's outputs, so they are not built here.

firmware-04 (`iwr1843_sar_lvds`) reuses this technique with its own modified demo copy.

## Compilers

`setenv.sh` pins ARM CGT **16.9.6.LTS** and C6000 **8.3.3** (plus XDC 3.50.08.24, BIOS 6.73.01.01);
the image's `ti-cgt-arm_20.2.7.LTS` is not used by SDK 3.6. `build.sh` prints both compiler
banners and records them in `build/build_info.txt` (`compilers_run=`).

## Outputs (`build/`)

`iwr1843_demo.bin`, `iwr6843_demo.bin` (flash images), `iwr1843_demo.elf`, `iwr6843_demo.elf`
(MSS executables, `.xer4f`), `*_demo_{mss,dss}.map`, `build_info.txt`, `compilers.txt`.

## Status

- Build: `./fw build ti_stock_demos` passes (firmware-03, 2026-10-05); ARM CGT 16.9.6 and C6000 8.3.3 ran;
  `/opt/ti` was not modified (no file newer than a stamp taken before the build).
- Equivalence to TI's prebuilt / the old in-tree build: not byte-identical, by design of the path.
  Two builds here give byte-identical `.bin` files, but the `.bin` size depends on the build path
  (assert/`__FILE__` strings land in `.text`): iwr6843 is 550660 bytes built in `/opt/ti`, 550788 here,
  550916 with a longer project name; TI's own prebuilt is 551044. The iwr1843 `.bin` is 324804 bytes in
  all three (TI, old, new). Section layout (memory map, `.const`, `.cinit`) matches; only `.text` and
  path-bearing `.const` sizes move. Compare with `diff` on the MEMORY CONFIGURATION / SEGMENT
  ALLOCATION MAP part of `build/*_demo_{mss,dss}.map`.
- On-board: not tested (out of scope).

## Build, flash, run

From `firmware_dev/`:

```bash
./fw build ti_stock_demos            # both families; or: ./fw build ti_stock_demos 18xx
./fw flash ti_stock_demos /dev/serial/by-id/<...>-if00 projects/ti_stock_demos/build/iwr1843_demo.bin
```

Bench steps: [`docs/bench_flash_iwr1843_demo.md`](docs/bench_flash_iwr1843_demo.md).

`./fw flash` (IWR1843 only) uses UniFlash 9.6.0's DSLite with `configs/iwr1843_uniflash.ccxml`, exactly as
`iwr1843_sar_lvds` does, and gets the same host gates (by-id `-if00` port, typed `FLASH MODE CONFIRMED` on a TTY).
Board in SOP 101 and power-cycled (USB + 5 V) before every attempt; add `--dry-run` to check the command and image
sha256 with no board. Success = `SUCCESS!! File type META_IMAGE1`. Confirmed on the bench 2026-10-07 (firmware-19 Step 2: rc=0, no
trailing `Can't Run Target CPU`, demo boots to `mmwDemo:/>` in SOP 001). `flash.sh` refuses `iwr6843_demo.bin`
(exit 2): flash the 6843 by hand (flashing-mode SOP0+SOP2, UniFlash GUI, Format = "bin", then functional mode).
After flashing, set functional mode (SOP0 only), power-cycle, and send a `configs/xwr*/profile_*.cfg` over the CLI
port (115200 baud); a cfg is accepted once per power-up.

# Build and flash notes, troubleshooting

## How the build works

Same out-of-tree technique as `projects/ti_stock_demos` (see its README). `build.sh` deletes `build/` and recreates an
SDK overlay in `build/sdk/`: `packages/` is a symlink farm onto the SDK, except `ti/demo/xwr18xx/mmw`, which is a
fresh copy of this project's `src/` (TI prebuilt outputs and `docs/` excluded). Every build is a clean build of the
current `src/`, so local edits are always picked up. After sourcing TI's `setenv.sh` it sets
`MMWAVE_SDK_DEVICE=iwr18xx` and `MMWAVE_SDK_INSTALL_PATH=build/sdk/packages`, then runs `make mmwDemo` (the MSS-only
metaimage).

Outputs in `build/`: `iwr1843_sar_lvds.bin` (flash image), `iwr1843_sar_lvds.elf` (MSS `.xer4f`),
`iwr1843_sar_lvds_mss.map`, `build_info.txt`, `compilers.txt`. Sizes and map facts: [`changes_vs_ti.md`](changes_vs_ti.md).

## Flash

`./fw flash iwr1843_sar_lvds <by-id -if00 port> --dry-run` prints the DSLite command and image sha256 and touches no
port. `flash.sh` calls UniFlash 9.6.0's `DSLite` (in the Docker image) with `configs/iwr1843_uniflash.ccxml` and
`-f <image>,1`. A real flash runs only from your own interactive terminal: `fw` prints the checklist (SOP0+SOP2 closed
= flashing mode, nothing else on the port) and you must type `FLASH MODE CONFIRMED`; there is no bypass. It refuses
without a TTY and if any process holds the port. A port that is not a `/dev/serial/by-id/...-if00` path (that XDS110
port is the bootloader port) is allowed only at a TTY after you also type `USE THIS PORT`; `--plan`, `--confirm` and
`--json` refuse it. Success is `SUCCESS!! File type META_IMAGE1` and DSLite rc=0 (`Flashed (DSLite rc=0)`).
Afterwards set functional mode (SOP0 only), power-cycle, and send a cfg over the CLI port at 115200 baud. Fallback:
the UniFlash GUI ([`bench_bringup.md`](bench_bringup.md) section 3).

Restore the stock demo with `./fw flash ti_stock_demos <port>` (image `projects/ti_stock_demos/build/iwr1843_demo.bin`).

The TI profiles in `configs/` of other projects are references only: they contain object-detection commands this
firmware rejects. To reconfigure, send `sensorStop`, `flushCfg`, the full cfg and `sensorStart`; no power cycle is
needed unless `channelCfg`, `adcCfg` or `lowPower` change.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Received unexpected data` / `Not able to connect to serial port` on flash | Power-cycle (USB + 5 V) before every attempt, retries included; a retry without one fails this way. |
| `File Order number value 0 not in valid range (1-4)` | DSLite was given a bare image; the `,1` in `-f <image>,1` is required (already in `flash.sh`). |
| DSLite log grows to tens of MB | `-e` is DSLite `--verbose`, not erase; it is not passed. Logs go to `build/`. |
| CLI rejects a cfg line (`guiMonitor`, `cfarCfg`, ...) | The stock TI cfg uses object-detection commands this firmware removed; use `configs/sar_example_2ms.cfg`. |
| `Ignored: Sensor is already started` | A finished finite-`numFrames` run still needs `sensorStop` first. |
| `channelCfg` / `adcCfg` / `lowPower` rejected after a restart | They are applied on the first start only: power-cycle to change them, or re-send the original values. |
| Prompt is `mmwDemo:/>` | The board runs the stock demo or an older SAR image; the current image prints `mm_sar_lvds:/>`. |
| Capture fails check 1-4 | See [`tuning_guide.md`](tuning_guide.md) and [`lvds_data_format.md`](lvds_data_format.md) section 1. |

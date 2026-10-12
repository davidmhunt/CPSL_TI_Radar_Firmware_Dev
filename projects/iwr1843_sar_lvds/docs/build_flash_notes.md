# Build and flash notes, troubleshooting

## Build

`./fw build iwr1843_sar_lvds` builds out of tree like `ti_stock_demos`; the shared SDK overlay mechanism (symlink farm in `build/sdk/`, the
project's `src/` copied over `ti/demo/xwr18xx/mmw`, `setenv.sh`, `MMWAVE_SDK_INSTALL_PATH`) is in
[`../../../docs/reference/build_environment.md`](../../../docs/reference/build_environment.md). Every build is a clean build of the current `src/`.
This project sets `MMWAVE_SDK_DEVICE=iwr18xx` and runs `make mmwDemo` (the MSS-only metaimage). Outputs in `build/`: `iwr1843_sar_lvds.bin`
(flash image), `iwr1843_sar_lvds.elf` (MSS `.xer4f`), `iwr1843_sar_lvds_mss.map`, `build_info.txt`, `build_info.json` (written by `fw`),
`compilers.txt`. Sizes and map facts: [`changes_vs_ti.md`](changes_vs_ti.md).

## Flash

Steps, jumpers and the typed confirmations: README and [`bench_bringup.md`](bench_bringup.md) section 3. `flash.sh` calls UniFlash 9.6.0's `DSLite`
(in the Docker image) with `configs/iwr1843_uniflash.ccxml` and `-f <image>,1`; `./fw flash ... --dry-run` prints the command and the image sha256
and touches no port. A port that is not a `/dev/serial/by-id/...-if00` path (that XDS110 port is the bootloader port) is allowed only at a TTY after
you also type `USE THIS PORT`; `--plan`, `--confirm` and `--json` refuse it. Success is `SUCCESS!! File type META_IMAGE1` and DSLite rc=0
(`Flashed (DSLite rc=0)`).

Restore the stock demo with `./fw flash ti_stock_demos <port>` (build it first with `./fw build ti_stock_demos`; image `projects/ti_stock_demos/build/iwr1843_demo.bin`, sha256 `cd43e0dfccc8a96a9ffc571ea36a365830eaba7951000892f02078fd2bdac559`; `./fw list` shows its status).

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

# Firmware projects

Each folder here is one self-contained firmware project. Run everything from `firmware_dev/`; setup and the
build, flash, publish path are in the [top README](../README.md) and [`docs/tutorials/`](../docs/tutorials/01_build_env.md).

| Project | Board | What it is |
|---------|-------|------------|
| [`ti_stock_demos`](ti_stock_demos/README.md) | IWR1843BOOST, IWR6843ISK | TI's unmodified SDK 3.6 mmw demos, built out of tree |
| [`iwr1843_sar_lvds`](iwr1843_sar_lvds/README.md) | IWR1843BOOST | SAR / LVDS raw-ADC firmware derived from the SDK 3.6 mmw demo |
| [`awr2243_cascade_ddm`](awr2243_cascade_ddm/README.md) | AWR2243 cascade EVM | AM273x + 2x AWR2243 DDM demo, flashed over the UART bootloader |
| [`_template`](_template/README.md) | any | Starting point for a new project (`./fw new <name>`; [tutorial 5](../docs/tutorials/05_new_project.md)) |

Current status and build state of each: `./fw list`.

## Reference

For developers, in [`docs/reference/`](../docs/reference/fw_cli.md):

- [`fw_cli.md`](../docs/reference/fw_cli.md): every `./fw` verb, flash gates, `--plan`/`--confirm`, `--json`, exit codes
- [`projects.md`](../docs/reference/projects.md): project layout, `project.toml`, README headings, build state
- [`build_environment.md`](../docs/reference/build_environment.md): the container, compose services, SDK overlay

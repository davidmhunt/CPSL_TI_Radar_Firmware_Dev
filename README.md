# CPSL TI Radar Firmware Development

Build, flash and check the firmware that runs on our TI mmWave radar boards. Everything happens in a
Docker container, so you do not install any TI software. Each step is one `./fw` command.

Run every command from this folder (`firmware_dev/`). `./fw help` lists them all.

## Before you start

You need:

- Linux, with Docker and `docker compose`. Your user must be in the `docker` group.
- Your user in the `dialout` group, to reach serial ports.
- About 25 GB of free disk and Python 3.11 or newer (`./fw` is a stdlib-only script; install no packages).
- A serial terminal such as `picocom` for bench work.

## The path

| # | Step | Command | Tutorial |
|---|------|---------|----------|
| 1 | Get this folder (once) | `git submodule update --init --checkout firmware_dev && cd firmware_dev` (from the parent repo root) | - |
| 2 | Set up the build environment (once) | `./downloads/download.sh && docker compose build` | [1](docs/tutorials/01_build_env.md) |
| 3 | Build a project | `./fw build <project>` | [2](docs/tutorials/02_build.md) |
| 4 | Flash it (a person at the bench) | `./fw flash <project> <port>` | [3](docs/tutorials/03_flash_verify.md) |
| 5 | Verify it | `./fw verify <project> --port <port>` | [3](docs/tutorials/03_flash_verify.md) |

`firmware_dev/` is normally a submodule of the parent repo `CPSL_TI_Radar`: clone that first. The parent sets
`update = none`, so `--checkout` is needed to fetch this folder.

Then you can:

- Publish a checked build for other lab users: [tutorial 4](docs/tutorials/04_publish.md).
- Start your own project: [tutorial 5](docs/tutorials/05_new_project.md).

## Projects

Each folder in `projects/` is one firmware. `./fw list` shows which are built and checked.

Boards: IWR1843 and IWR6843 are single-chip radars; the AWR2243 cascade is two AWR2243 chips plus an AM273x
processor on one EVM.

| Project | Board | What it is |
|---------|-------|------------|
| [`awr2243_cascade_ddm`](projects/awr2243_cascade_ddm/README.md) | AWR2243 cascade EVM | Two-chip cascade DDM demo |
| [`iwr1843_sar_lvds`](projects/iwr1843_sar_lvds/README.md) | IWR1843 | SAR / LVDS raw-ADC streaming firmware |
| [`ti_stock_demos`](projects/ti_stock_demos/README.md) | IWR1843, IWR6843 | TI's own `mmw` demos, unchanged |

## Two rules to remember

- Flashing needs a person. Boards are used by one person at a time: ask the lab first. Jumper and
  switch settings (SOP switches or the J6 jumper: flash mode vs run mode) are in each project README, under "Flash".
- The cascade board accepts a chirp config (the radar's settings, sent as "cfg" commands) once per power-up.
  Power-cycle it before every run that sends one.

Terms: [glossary](docs/reference/fw_cli.md#glossary). Board settings and bench checks: your project README.

# CPSL TI Radar Firmware Development

Build, flash, verify and publish TI mmWave radar firmware for the CPSL TI Radar stack, headlessly, in a
Docker build environment. No Code Composer Studio GUI, MATLAB or host TI install is needed. Every step
below is one `./fw` verb, run from this folder (`firmware_dev/`). `./fw help` lists them.

## The path

Prerequisites (the one place they are listed): Linux; Docker with `docker compose`, and your user in the
`docker` group (or rootless Docker); about 25 GB of disk; Python 3.11+ (the `fw` logic is stdlib only); [`uv`](https://docs.astral.sh/uv/) (optional: only for `fw`'s own tests, `uv run --group dev pytest tests`, and the SAR bench tools, `--group tools`);
membership of the `dialout` group for serial ports. Bench work also wants a serial terminal such as `picocom`.

| # | Step | Command | Tutorial |
|---|------|---------|----------|
| 1 | Fetch this folder (once) | from the parent repo root: `git submodule update --init --checkout firmware_dev && cd firmware_dev`; every command below runs here | - |
| 2 | Set up the build environment (once) | `./downloads/download.sh && docker compose build` then `./fw deps` | [01](docs/tutorials/01_build_env.md) |
| 3 | Build a project | `./fw build <project>` | [02](docs/tutorials/02_build.md) |
| 4 | Flash it (a human at the bench) | `./fw flash <project> <port>` | [03](docs/tutorials/03_flash_verify.md) |
| 5 | Verify it | `./fw verify <project> --port <port>` | [03](docs/tutorials/03_flash_verify.md) |

Then, as follow-ons: publish a verified build to `shipped_firmware/` with `./fw publish <project>`
([04](docs/tutorials/04_publish.md)), or add your own project with `./fw new <project>`
([05](docs/tutorials/05_new_project.md)).

## Projects

Each folder in `projects/` is one self-contained firmware (source, cfgs, scripts, docs, tests). Current
projects, from `./fw list` (the live status and build state are in that command, not here):

| Project | Board | SDK | What it is |
|---------|-------|-----|------------|
| [`awr2243_cascade_ddm`](projects/awr2243_cascade_ddm/README.md) | AWR2243 cascade (AM273x) | mmwave_mcuplus_sdk 04.04.00.01 | Two-chip cascade DDM demo |
| [`iwr1843_sar_lvds`](projects/iwr1843_sar_lvds/README.md) | IWR1843 | mmwave_sdk 03.06.02.00-LTS | SAR / LVDS raw-ADC streaming firmware |
| [`ti_stock_demos`](projects/ti_stock_demos/README.md) | IWR1843, IWR6843 | mmwave_sdk 03.06.02.00-LTS | TI's unmodified `mmw` demos, built out of tree |

## Rules you will meet

- **Flashing is gated.** `fw flash` refuses unless the port exists, no process holds it, and a human types
  `FLASH MODE CONFIRMED` at a terminal. A by-id `-if00` port (`/dev/serial/by-id/...-if00`) is expected; at a
  terminal any other port is allowed only after you also type `USE THIS PORT`. Scripts use
  `--plan` then `--confirm <token>` and by-id ports only. Boards are single-user (coordinate with the lab before flashing) and need a person for
  jumpers, switches and power. Details: [tutorial 3](docs/tutorials/03_flash_verify.md).
- **A cfg is accepted once per power-up on the cascade only**: power-cycle before every cascade run that
  sends one. IWR boards accept a cfg again without a power-cycle (the host driver's firmware descriptors say so).

## Where to go next

- Verb, manifest and exit-code reference: [`projects/README.md`](projects/README.md).
- Full machine contract: [`docs/specs/fw_project_contract.md`](../docs/specs/fw_project_contract.md) (parent repo).
- Facts for the whole stack (toolchain versions, boards, quirks): [`docs/firmware.md`](../docs/firmware.md) (parent repo).
- Each project README: build options, flash mode, bench check, and what it changes versus TI.

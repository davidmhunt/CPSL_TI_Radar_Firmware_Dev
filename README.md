# CPSL TI Radar Firmware Development

Build, flash, verify and publish TI mmWave radar firmware for the CPSL TI Radar stack, headlessly, in a
Docker build environment. No Code Composer Studio GUI, MATLAB or host TI install is needed. Every step
below is one `./fw` verb, run from this folder (`firmware_dev/`). `./fw help` lists them.

## The path

| # | Step | Command | Tutorial |
|---|------|---------|----------|
| 1 | Set up the build environment (once) | `./downloads/download.sh && docker compose build` then `./fw deps` | [01](docs/tutorials/01_build_env.md) |
| 2 | Build a project | `./fw build <project>` | [02](docs/tutorials/02_build.md) |
| 3 | Flash and verify it (a human at the bench) | `./fw flash <project> <port>` then `./fw verify <project> --port <port>` | [03](docs/tutorials/03_flash_verify.md) |
| 4 | Publish it to `shipped_firmware/` | `./fw publish <project>` | [04](docs/tutorials/04_publish.md) |
| 5 | Add your own project | `./fw new <project>` | [05](docs/tutorials/05_new_project.md) |

Prerequisites: Linux, Docker with `docker compose`, about 25 GB of disk, Python 3.11+ (the `fw` logic is
stdlib only), membership of the `dialout` group for serial ports, and `git lfs` for the PDF docs
(`git lfs install` once, `git lfs pull` after cloning). This folder is an opt-in submodule of
`CPSL_TI_Radar`; on a fresh clone fetch it with
`git submodule update --init --checkout firmware_dev` from the parent root.

## Projects

Each folder in `projects/` is one self-contained firmware (source, cfgs, scripts, docs, tests). Current
projects, from `./fw list` (the live status and build state are in that command, not here):

| Project | Board | SDK | What it is |
|---------|-------|-----|------------|
| [`awr2243_cascade_ddm`](projects/awr2243_cascade_ddm/README.md) | AWR2243 cascade (AM273x) | mmwave_mcuplus_sdk 04.04.00.01 | Two-chip cascade DDM demo |
| [`iwr1843_sar_lvds`](projects/iwr1843_sar_lvds/README.md) | IWR1843 | mmwave_sdk 03.06.02.00-LTS | SAR / LVDS raw-ADC streaming firmware |
| [`ti_stock_demos`](projects/ti_stock_demos/README.md) | IWR1843, IWR6843 | mmwave_sdk 03.06.02.00-LTS | TI's unmodified `mmw` demos, built out of tree |

## Rules you will meet

- **Flashing is gated.** `fw flash` refuses unless the port is a by-id `-if00` port, no process holds it,
  and a human types the confirmation phrase at a terminal. Scripts use `--plan`, then `--confirm <token>`.
  Boards are single-user and need a person for jumpers and power.
- **A cfg is accepted once per power-up** on the cascade (and the IWR demos): power-cycle before every run
  that sends one.
- **Builds are traceable.** `fw build` writes `build/build_info.json` (commit, sha256 of each artifact);
  `fw publish` refuses a dirty or stale build.
- **Only source is committed.** TI SDKs, compilers and installers are downloaded, never tracked.

## Where to go next

- Verb, manifest and exit-code reference: [`projects/README.md`](projects/README.md).
- Full machine contract: `docs/specs/fw_project_contract.md` in the parent repo.
- Facts for the whole stack (toolchain versions, boards, quirks): `docs/firmware.md` in the parent repo.
- Each project README: build options, flash mode, bench check, and what it changes versus TI.

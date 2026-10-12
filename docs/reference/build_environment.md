# Build environment reference

For developers. The image and how `fw` drives it (setup steps: [tutorial 1](../tutorials/01_build_env.md)).
Sources: `Dockerfile`, `docker-compose.yaml`, `tools/fwcli/procs.py`.

## Image and compose services

One image, `cpsl-ti-radar-firmware-dev:latest`, built by `docker compose build` from `downloads/` (filled by
`downloads/download.sh`). TI tools sit under `/opt/ti`, root-owned, so builds running as your user cannot write
there. `docker-compose.yaml` defines two services from it:

| Service | Used by | Difference |
|---------|---------|------------|
| `firmware-env` | `build`, `test`, `--dry-run`, browsing the SDK | no serial devices |
| `flash` | real flashes (`flash` without `--dry-run`) | extends `firmware-env`; adds `/dev` and the `dialout`, `plugdev` groups, plus cgroup rules for ttyUSB (188) and ttyACM (166), so ports that re-enumerate after a power-cycle stay visible |

Both bind-mount `firmware_dev/` at `/build_context` (`/build_context/projects/<p>/x` is `projects/<p>/x`
on the host). Git metadata is not usable inside, hence `FW_COMMIT`.

## What `fw` runs

`docker compose run --rm --user <uid>:<gid> -e HOME=/tmp/fwhome -T -e K=V ... <service> bash -c 'mkdir -p "$HOME" && exec "$0" "$@"' /build_context/projects/<p>/<script> [args]`

- `-T`: no TTY. Passed: `FW_PROJECT`, `FW_VARIANT` (build), `FW_PORT`, `FW_IMAGE`, `FW_DRY_RUN` (flash), `FW_COMMIT`
  (set by `fw`), plus `CCS_CONFIG` and any other `FW_*` from your shell, except `FW_COMMIT`, `FW_ROOT`, `FW_COMPOSE`.

## Toolchain paths in the image

`ENV` lines in the `Dockerfile` (mirrored in `docker-compose.yaml`) set `MMWAVE_SDK_PATH`, `MMWAVE_MCUPLUS_SDK_PATH`,
`CGT_TI_{ARM_CLANG,C6000,ARM}_PATH`, `SYSCONFIG_PATH`, `RADAR_TOOLBOX_INSTALL_PATH`, `UNIFLASH_PATH` and
`CCS_INSTALL_PATH`, all under `/opt/ti`. Versions: [`docs/firmware.md`](../../../docs/firmware.md) (parent repo).

## Out-of-tree SDK overlay (`ti_stock_demos`, `iwr1843_sar_lvds`)

TI's mmw demo sources include `<ti/demo/xwr18xx/mmw/...>` and its makefiles write objects and images beside the
sources, so the demo must sit at that path under an SDK root that is not `/opt/ti`. Both projects' `build.sh`
(copies of one technique; neither sources the other) therefore build `build/sdk/`:

- `build/sdk/packages/` is a symlink farm: each SDK `packages/*` entry links to the SDK, except the directories
  on the path to the demo folder, which are real directories. `ti_stock_demos` keeps `ti/demo/xwr18xx/mmw`
  and `ti/demo/xwr68xx/mmw` (per selected family); `iwr1843_sar_lvds` keeps `ti/demo/xwr18xx/mmw`. The demo
  folder is a real copy: TI's folder for the stock project, the project's `src/` for the SAR one, in both
  cases without `*.bin`, `*.map`, `*.xer4f`, `*.xe674`, `*.rov.xs` (make would take them as up to date) and `docs`.
- `build/sdk/firmware` links to the SDK's `firmware/` (radar subsystem images). TI's `setenv.sh` is sourced from
  `packages/scripts/unix` (default `MMWAVE_SDK_DEVICE=iwr68xx`); then the script sets `MMWAVE_SDK_DEVICE` and
  points `MMWAVE_SDK_INSTALL_PATH` at `build/sdk/packages`.
- Target: `make mmwDemo` for 18xx (TI's 18xx `all` also builds the AOP variants); `make all` for 68xx (no `mmwDemo`).
- Compilers come from `setenv.sh`: ARM CGT 16.9.6.LTS, C6000 8.3.3, XDC 3.50.08.24, BIOS 6.73.01.01; the image's
  `ti-cgt-arm_20.2.7.LTS` is unused by SDK 3.6. Both banners go to `build/compilers.txt`.
- `build.sh` removes `build/` first, so a one-variant build deletes the other family's outputs.
- Binaries embed the build path in `.text`: two builds in the same folder are byte-identical; a rebuild under
  another folder name matches TI's prebuilt in layout but not byte for byte.

The cascade project builds with CCS headless instead; `CCS_CONFIG` (`Release` default, `Debug`) picks the configuration.

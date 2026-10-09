# 1. Set up the build environment

Goal: a Docker image that holds every TI compiler and SDK, so no TI software is installed on your machine.
Do this once per machine. Run everything from `firmware_dev/`. Prerequisites (Docker, the `docker` and
`dialout` groups, disk) are listed in the [top README](../../README.md#the-path).

## Fetch the TI installers

```bash
./downloads/download.sh     # about 3.6 GB into downloads/ (gitignored); safe to re-run, it resumes
```

The script downloads: mmWave MCU+ SDK, mmWave SDK, SysConfig, TI Arm Clang, C6000 and ARM CGT compilers,
the Radar Toolbox, Code Composer Studio (headless) and UniFlash 9.6.0. Exact versions are pinned in the
script and listed in [`docs/firmware.md`](../../../docs/firmware.md) (parent repo). If TI's server refuses a download, fetch the
file by hand from the TI download page, keep the exact filename the script uses, and put it in
`downloads/`.

## Build the image

```bash
docker compose build        # builds cpsl-ti-radar-firmware-dev:latest; long, once
```

It fails early with a missing `COPY` source when an installer is absent: re-run `download.sh` first.

## Check it

```bash
./fw deps                   # prints "deps: all download items present" when nothing is missing
./fw list                   # prints the project table
./fw deps ti_stock_demos    # the installers one project needs
```

`fw build` runs the same check before every build, so a missing installer stops it with exit 4 and the
item number to fetch.

## The two compose services

`docker-compose.yaml` defines two services from the one image. `fw` picks the right one; you rarely type them.

| Service | Used for | Difference |
|---------|----------|------------|
| `firmware-env` | builds, `fw test`, dry runs, browsing the SDK | no serial devices |
| `flash` | real flashes and the cascade serial check | same image plus `/dev` and the `dialout`/`plugdev` groups, so serial ports (including ones that re-enumerate after a power-cycle) are visible |

Both bind-mount `firmware_dev/` at `/build_context` inside the container, so
`/build_context/projects/<p>/...` is the same file as `projects/<p>/...` on the host.

```bash
docker compose run --rm firmware-env       # a bash shell inside the image, TI tools under /opt/ti
docker compose run --rm flash <command>    # a command with serial access (see the project READMEs)
```

Browse the SDK sources under `/opt/ti/`. The container does not see git metadata; `fw` passes the commit in as `FW_COMMIT`.

## Next

[2. Build a project](02_build.md).

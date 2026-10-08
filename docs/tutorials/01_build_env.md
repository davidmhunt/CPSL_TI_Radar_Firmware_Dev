# 1. Set up the build environment

Goal: a Docker image that holds every TI compiler and SDK, so no TI software is installed on your machine.
Do this once per machine. Run everything from `firmware_dev/`.

## Fetch the TI installers

```bash
./downloads/download.sh     # about 3.6 GB into downloads/ (gitignored); safe to re-run, it resumes
```

The script downloads: mmWave MCU+ SDK, mmWave SDK, SysConfig, TI Arm Clang, C6000 and ARM CGT compilers,
the Radar Toolbox, Code Composer Studio (headless) and UniFlash 9.6.0. Exact versions are pinned in the
script and listed in `docs/firmware.md` in the parent repo. If TI's server refuses a download, fetch the
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

## Interactive shell (optional)

```bash
docker compose run --rm firmware-env       # a bash shell inside the image, TI tools under /opt/ti
```

Browse the SDK sources under `/opt/ti/`. The container does not see git metadata; `fw` passes the commit in as `FW_COMMIT`.

## Next

[2. Build a project](02_build.md).

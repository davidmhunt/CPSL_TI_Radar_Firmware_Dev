# 1. Set up the build environment

You will make a Docker image that holds every TI compiler and SDK. Do this once per machine.
Run all commands from `firmware_dev/`. The requirements are in the [top README](../../README.md#before-you-start).

## 1. Download the TI installers

```bash
./downloads/download.sh
```

This fetches nine installers, about 3.6 GB, into `downloads/`. It is safe to run again; it resumes. If TI's
server refuses one, download it by hand from TI's site into `downloads/`, under the file name at the end of its
`curl` URL in `downloads/download.sh` (for example `mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin`). The
UniFlash file `uniflash_sl.9.6.0.5764.run` is checked against a pinned size and sha256 by the script.

## 2. Build the image

```bash
docker compose build
```

This takes a long time. It ends without an error and leaves the image `cpsl-ti-radar-firmware-dev:latest`
(`docker image ls cpsl-ti-radar-firmware-dev`). If it stops with a missing `COPY` source, an installer is
absent: run `download.sh` again.

## 3. Check it

```bash
./fw deps
./fw list
```

`./fw deps` prints `deps: all download items present` when nothing is missing. `./fw list` prints the
projects. To check one project's installers, run `./fw deps ti_stock_demos`.

How the containers work: [build environment reference](../reference/build_environment.md).

## Next

[2. Build a project](02_build.md).

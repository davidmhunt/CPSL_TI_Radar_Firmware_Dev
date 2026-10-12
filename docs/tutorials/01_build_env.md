# 1. Set up the build environment

You will make a Docker image that holds every TI compiler and SDK. Do this once per machine.
Run all commands from `firmware_dev/`. The requirements are in the [top README](../../README.md#before-you-start).

## 1. Download the TI installers

```bash
./downloads/download.sh
```

This fetches about 3.6 GB into `downloads/`. It is safe to run again; it resumes. If TI's server refuses a
file, download it by hand from TI's site. Keep the file name the script uses and put it in `downloads/`.

## 2. Build the image

```bash
docker compose build
```

This takes a long time. If it stops with a missing `COPY` source, an installer is absent: run
`download.sh` again.

## 3. Check it

```bash
./fw deps
./fw list
```

`./fw deps` prints `deps: all download items present` when nothing is missing. `./fw list` prints the
projects. To check one project's installers, run `./fw deps ti_stock_demos`.

## Optional: look inside

```bash
docker compose run --rm firmware-env
```

This opens a shell in the image. The TI tools are under `/opt/ti`. How the containers work:
[build environment reference](../reference/build_environment.md).

## Next

[2. Build a project](02_build.md).

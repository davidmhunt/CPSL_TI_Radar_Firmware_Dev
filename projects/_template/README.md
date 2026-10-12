# PROJECT_NAME

Replace each _italic note_ with a few lines and the commands. Put longer detail in `docs/` and link it.
Keep every `##` heading: `./fw test` checks them.

## Purpose

_One or two sentences: what this firmware does and which board it runs on._

## Status

_One line copied from `./fw list` for this project. No dates._

## Build

`./fw build PROJECT_NAME` (run from `firmware_dev/`).
_Add the `--variant` values, if any, and how long a build takes._

## Test

`./fw test PROJECT_NAME`
_One line per host test in `tests/`: what it checks._

## Flash

`./fw flash PROJECT_NAME <by-id port>`
_List the jumper or switch settings for flash mode, the power steps, and how to return to run mode.
Newcomers look here for them._

## Verify

`./fw verify PROJECT_NAME --port <by-id port>`
_Say whether the firmware can be probed safely and what a good answer looks like._

## Bench check

The checks to run on a real board are in [`docs/bench_check.md`](docs/bench_check.md).

## Layout

_List only folders that hold something specific to this project, one line each._

- `project.toml`: the manifest `./fw` reads
- `build.sh`, `flash.sh`: run in the container
- `src/`, `configs/`, `docs/`, `tools/`, `tests/`: source, chirp configs, notes, scripts, host tests

## Changes vs TI

_One bullet per change on top of the TI source, newest last._

- _none yet: `src/` is the unmodified TI baseline_

## Known limits

_A short list of what is untested or not supported._

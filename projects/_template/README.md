# PROJECT_NAME

## Purpose

One sentence: what this firmware does and which board it runs on.

## Status

Quote `./fw list` for this project (status and build state); no dates here, bench records live in `project.toml`.

## Build

From `firmware_dev/`: `./fw build PROJECT_NAME` (checks `./fw deps` first) writes `projects/PROJECT_NAME/build/`
and `build/build_info.json`. Say which `--variant` values exist, if any.

## Test

`./fw test PROJECT_NAME` runs the hardware-free checks (manifest, descriptors, cfgs, `[test].commands`,
`build --dry-run`). Name the host tests in `tests/` and what they pin.

## Flash

`./fw flash PROJECT_NAME <by-id port> [image]` (a human types the confirmation) or, for scripts,
`--plan` then `--confirm <token>`. Describe the board's flash mode (jumpers, power) and how to return to run mode.

## Verify

`./fw verify PROJECT_NAME` sends the descriptor's identify probes. State whether the firmware is safe to probe.

## Bench check

The user-run on-board check is `docs/bench_check.md`; results are recorded as `[[bench]]` in `project.toml`.

## Layout

| Path | What |
|------|------|
| `project.toml` | the manifest |
| `build.sh`, `flash.sh` | run in the container; read the standard `FW_*` environment |
| `src/`, `configs/`, `docs/`, `tools/`, `tests/` | source, tracked cfgs, notes, project-only scripts, host tests |

## Changes vs TI

The TI baseline is `[source]` in `project.toml`. List every change on top of it, newest last, so
`git diff <baseline_commit> -- src/` reads as expected.

- _(none yet: `src/` is the unmodified TI baseline)_

## Known limits

List what is untested or unsupported.

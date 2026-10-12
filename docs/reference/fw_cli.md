# `fw` CLI reference

For developers and scripts. Run `./fw` from `firmware_dev/`; logic: `tools/fwcli/`. `./fw help` is the summary,
`./fw <verb> --help` the arguments. Walk-throughs: [`../tutorials/`](../tutorials/01_build_env.md).

## Verbs

| Verb | What it does |
|------|--------------|
| `list` | Per project: effective status, build state, board, SDK, baseline, artifacts, `[[bench]]` records |
| `ports [<project>]` | By-id serial ports and whether a process holds them; with a project, filtered by `[flash].port_glob` (else `[verify].cli_port_glob`) |
| `new <project>` | Copy `projects/_template/` to `projects/<project>/`, replacing `PROJECT_NAME`. Names match `[a-z0-9][a-z0-9_]*`; refuses an existing folder (exit 2) |
| `deps [<project>]` | Check the installers named by `[deps].download_items` exist in `downloads/` (table below); exit 4 if not |
| `build <project> [--variant V] [--dry-run] [args...]` | Deps check, `build.sh` in the `firmware-env` container, then `build/build_info.json` |
| `test [<project>]` | Hardware-free checks (list below); `--skip-commands` skips `[test].commands` |
| `flash <project> <port> [image] [--dry-run \| --plan \| --confirm TOKEN]` | Gated flash |
| `verify <project> [--port P] [--dry-run]` | Descriptor identify probes; never a chirp cfg |
| `publish <project> [--dest D] [--dry-run]` | Copy flashable images and provenance to `shipped_firmware/` |

Without a project, `deps` and `test` cover every project and `ports` lists every by-id port. Build states
(`none`, `clean`, `dirty`, `stale`): [projects.md](projects.md#build-state).

`deps` items are `downloads/` globs: 1 `mmwave_mcuplus_sdk_*`, 2 `mmwave_sdk_*`, 3 `sysconfig-*`, 4 `ti_cgt_tms470_*`,
5 `ti_cgt_armllvm_*`, 6 `ti_cgt_c6000_*`, 7 `radar_toolbox_*`, 8 `CCS*`, 9 `uniflash_*`.

`build`: a first extra argument naming a declared variant counts as `--variant`; other extras pass to `build.sh`
after it. An unknown variant exits 2. `--dry-run` prints the container command and tolerates missing deps or
git. A real build holds lock `project-<name>` and writes `build/build_info.json` (project, commit, `dirty`,
variant, toolchain, UTC time, sha256 per artifact).

`test` checks: manifest validity; each artifact's descriptor exists in `CPSL_TI_Radar_cpp/config/firmware/`, is
valid JSON and its `identify.<board>.source` names this project and one of its artifacts; `[test].cfgs` globs
match tracked files; README and `bench_check.md` headings and the README date rule ([projects.md](projects.md#readme-and-bench_check-headings));
each `[test].commands` entry runs in the project folder; `build --dry-run` succeeds. `TODO` text only warns.

`flash` image: default is the first `[[artifact]]` in `build/`; a path must be a file inside `firmware_dev/`; for
`uart_uniflash` projects `prebuilt` selects the image baked into the container (not with `--plan`).
`--plan`, `--confirm` and `--dry-run` exclude one another (exit 2).

`verify`: no `--port` means the one by-id port matching `cli_port_glob` (zero or several: exit 4). The port must
be by-id, matching the glob, and free. It opens at `[verify].baud`, sends each probe, and applies the
descriptor's `require`/`reject` regexes: `outcome` `pass` or `fail` (exit 1). A probe that looks like a cfg
(`sensorStart`, `sensorStop`, `flushCfg`, `*Cfg`) is refused (exit 4). With `once_safe: false` in the identify
entry (the cascade) nothing is sent and the outcome is `skipped` (exit 0). `--dry-run` only lists the probes.

`publish`: refuses (exit 4) a build that is `none`, `dirty` or `stale`, or an artifact differing from the
build record. Only `flashable` artifacts with a non-empty `descriptor` publish; others are listed as `skipped`.
Each goes to `<D>/<BOARD>/<descriptor>/<file>` (default `D` = parent `shipped_firmware/`) with
`<file>.provenance.json`: project, `firmware_dev_commit`, variant, toolchain, `built_utc`, artifact, board,
descriptor, sha256, `chirp_cfg` (path and sha256 of every `[test].cfgs` match) and `published_utc`.

## Flash gates

| Gate | Check | Failure |
|------|-------|---------|
| G1 | Image is inside `firmware_dev/` | 4 |
| G2 | Port exists | 4 |
| G3 | No process holds the port (`fuser`, else `lsof`; neither installed: 4) | 4 |
| G4 | Port is `/dev/serial/by-id/...-if00`. At a terminal another port needs you to type `USE THIS PORT` | wrong phrase 5; under `--json` or `--plan` 4 |
| G5, G6 | The `[flash].mode_steps` checklist is printed; you type `FLASH MODE CONFIRMED` at a terminal (no terminal and no token: 5) | 5 |
| G7 | `--dry-run` flashes nothing and touches no port | |

`method = "manual"` prints `mode_steps` and `after_steps` and exits 3. A `manual_images` entry prints
`flash.sh`'s own steps for `--plan` and `--dry-run` and exits 3 with no token. A real flash holds lock
`port-<resolved device>`. If `flash.sh` exits 0 but its output lacks `[flash].success_marker`: exit 1.
`flash.sh` exit 3 or 2 passes through; anything else is 1. `after_steps` print on success.

## Scripted flashing (`--plan` / `--confirm`)

No script may type the phrase. `--plan` runs G1 to G4 (by-id ports only), prints the checklist and returns a
token; a human does the bench steps; `--confirm TOKEN` flashes.

- The token is 32 lowercase hex characters stored in `.fw/tokens/` (mode 600), valid 300 s, single use: it is
  consumed even if a later check fails. A new `--plan` for the same resolved port deletes older tokens.
- It records project, port string, resolved port, image path and sha256. A different project, port or image, or
  an unknown, expired, used or malformed token: exit 5. `--confirm` re-runs G2 to G4 and recomputes the sha256;
  a changed image exits 4 ("plan again").

## `--json`

Add `--json` anywhere. stdout is JSON Lines, human text goes to stderr. Progress events:
`{"event":"progress","verb","stage","message"[,"percent"]}` (`percent` from `[ NN%]` in build output); then exactly one
`{"event":"result","verb","ok","code","reason","data"}`, even after a crash (code 1). `data`: `flash --plan` has
`token`, `expires_at`, `checklist`, `sha256`, `image`, `port`, `after_steps`; `verify` `outcome`, `probes`; `build`
`build_info`; `publish` `paths`, `skipped`; `list` `projects`.

## Exit codes

`0` ok. `1` failed (build, flash or probe failed; cancelled). `2` usage error. `3` unsupported, or manual steps
printed. `4` precondition refused (missing deps, git unavailable, a gate, a lock, build not clean). `5`
confirmation missing or invalid.

SIGINT or SIGTERM kills the child process group, prints `after_steps` (the board may still be in flash mode)
and ends with code 1, reason `cancelled`. A lock whose process is dead is reclaimed; a live one gives exit 4.

## Environment

- `FW_COMMIT`, set by `fw` for `build.sh` and `flash.sh`: the short hash of `firmware_dev` HEAD. `build` appends
  `-dirty` when `git status --porcelain -- projects/<project> fw tools` is not empty; `flash` passes the bare
  hash. Git unavailable: `build` exits 4. Container variables: [build_environment.md](build_environment.md).
- Test seams: `FW_ROOT`, `FW_SERIAL_BYID_DIR`, `FW_COMPOSE` (replaces `docker compose`). State: `.fw/`, gitignored.

# Firmware projects: reference

Reference for the `projects/` folder, the `project.toml` manifest and the `./fw` verbs. The newcomer path
(setup, build, flash, publish, add a project) is in the [top-level README](../README.md) and
[`docs/tutorials/`](../docs/tutorials/01_build_env.md); the full contract is
[`docs/specs/fw_project_contract.md`](../../docs/specs/fw_project_contract.md) (parent repo). Prerequisites
(Docker and the `docker` and `dialout` groups, disk) are in the [top README](../README.md#the-path).
Run everything from `firmware_dev/`.

## Layout

Each folder in `projects/` is one self-contained firmware; changing one never breaks another. Shared:
the toolchain image, `tools/` (`fwcli/` is the `fw` logic) and `downloads/`. A script used by one project
lives in that project's `tools/`.

| Path in a project | What it is for |
|-------------------|----------------|
| `README.md` | Fixed headings from [`_template/README.md`](_template/README.md) |
| `project.toml` | The manifest `./fw` reads (below) |
| `build.sh`, `flash.sh` | Run in the container; read the standard `FW_*` environment; `build.sh` writes only to `build/` |
| `src/`, `configs/`, `tools/`, `tests/`, `docs/` | Source (starts as the unmodified TI baseline), tracked cfgs, project-only scripts, hardware-free host tests, notes and `bench_check.md` |
| `build/` | Outputs and `build_info.json`; gitignored |

## `project.toml`

Tables: `[project]` (name, summary, status `stub|source|built|bench`), `[deps]` (sdk, sdk_version,
toolchain, download_items), `[source]` (baseline, baseline_commit), `[build]` (script, variants,
est_minutes), `[[artifact]]` (file, board, descriptor, flashable; the first is the default image),
`[flash]` (method `dslite|uart_uniflash|manual`, gate `sop|j6`, port_glob, mode_steps, after_steps,
success_marker, optional manual_images), `[verify]`, `[test]` (commands, cfgs, bench_doc) and `[[bench]]`
(board, date, sha256, result `pass|partial|fail`, doc). Which tables each status needs is commented in
[`_template/project.toml`](_template/project.toml). Edits to `[[bench]]` and the `[project].status` line
do not make a build dirty or stale; any other text change does.

## The `fw` verbs

`./fw help` is the summary and `./fw <verb> --help` the arguments. Every verb accepts `--json` (JSON Lines,
one final `result`).

| Verb | What it does |
|------|--------------|
| `list` | Projects with status, board, SDK, baseline and build state (`clean`, `dirty`, `stale`, `none`) |
| `ports [<project>]` | Candidate by-id serial ports and whether a process holds them |
| `new <project>` | Copy `_template/` to `projects/<project>/` |
| `deps [<project>]` | Check the TI installers are in `downloads/` |
| `build <project> [--variant V] [--dry-run] [args]` | Deps check, `build.sh` in the container, `build/build_info.json` |
| `test [<project>]` | Hardware-free checks; `--skip-commands` skips `[test].commands` |
| `flash <project> <port> [image] [--dry-run \| --plan \| --confirm TOKEN]` | Gated flash |
| `verify <project> [--port P]` | Descriptor identify probes, never a chirp cfg |
| `publish <project> [--dest D] [--dry-run]` | Images and provenance to `shipped_firmware/` |

Exit codes: `0` ok, `1` failed, `2` usage, `3` unsupported or manual steps printed, `4` precondition
refused, `5` confirmation missing or invalid.

## Notes

- `fw` sets `FW_COMMIT` to the short hash of `firmware_dev` HEAD, with `-dirty` if `git status --porcelain` lists anything.
- A board is single-user: one flash or capture at a time. Serial ports: `ls -l /dev/serial/by-id/`.

# Projects reference

For developers. A project is one folder in `projects/`; `./fw` reads its `project.toml`. Contract:
[`docs/specs/fw_project_contract.md`](../../../docs/specs/fw_project_contract.md) (parent repo). New project:
[`projects/_template/`](../../projects/_template/), [tutorial 5](../tutorials/05_new_project.md). Verbs: [fw_cli.md](fw_cli.md).

## Folder layout

Projects are self-contained. Shared: the toolchain image, `tools/` (`tools/fwcli/` is the `fw` logic) and `downloads/`.

| Path in a project | What it is for |
|-------------------|----------------|
| `project.toml`, `README.md` | The manifest `./fw` reads; the README with fixed headings (below) |
| `build.sh`, `flash.sh` | Run in the container from their own folder; read the standard `FW_*` environment; `build.sh` writes only to `build/` and does not call `git` (the container cannot see it) |
| `src/` | Source; starts as the unmodified TI baseline, committed on its own |
| `configs/`, `tools/`, `tests/`, `docs/` | Tracked cfgs and flasher configs, project-only scripts, hardware-free host tests, notes and `bench_check.md` |
| `build/` | Outputs plus `build_info.json`; gitignored |

`build.sh` reads `FW_PROJECT`, `FW_VARIANT` (empty if none; also `$1`) and `FW_COMMIT`. `flash.sh` reads
`FW_PROJECT`, `FW_PORT`, `FW_IMAGE` (container path), `FW_DRY_RUN` (`1` = print only), also as arguments
`flash.sh [--dry-run] <port> <image>`. Its exit codes: `0` flashed and confirmed, `1` failed, `2` bad
arguments, `3` no headless flasher (manual steps printed).

## `project.toml`

Unknown tables and keys are errors; `*` = required. Annotated example: [`_template/project.toml`](../../projects/_template/project.toml).

| Table | Keys |
|-------|------|
| `[project]` | `name*` (equals the folder name), `summary*`, `status*`: `stub`, `source`, `built`, `bench` |
| `[deps]` | `sdk*`, `sdk_version*`, `toolchain*`, `download_items*` (numbers, see [fw_cli.md](fw_cli.md#verbs)) |
| `[source]` | `baseline*` (TI source path and version), `baseline_commit*` (`firmware_dev` commit that added it unmodified) |
| `[build]` | `script*`, `est_minutes*`, `variants` |
| `[[artifact]]` | `file*`, `board*`, `descriptor*` (id in `CPSL_TI_Radar_cpp/config/firmware/`, `""` for none), `flashable*`; the first is the default image |
| `[flash]` | `method*` (`dslite`, `uart_uniflash`, `manual`), `gate*` (`sop`, `j6`), `port_glob*`, `mode_steps*`, `after_steps*`, `success_marker*` (flasher's success text), `manual_images`. `mode_steps` is the checklist shown before a flash, `after_steps` is printed after it and on cancel |
| `[verify]` | `cli_port_glob*`, `baud*`, `descriptor*` |
| `[test]` | `commands*`, `cfgs*` (globs relative to the project, never `build/`), `bench_doc` (default `docs/bench_check.md`) |
| `[[bench]]` | `board*`, `date*` (`YYYY-MM-DD`), `sha256*` (64 lowercase hex), `result*` (`pass`, `partial`, `fail`), `doc*` |

Required tables by status: `stub` needs `[project]`, `[test]`; `source`, `built` and `bench` also need `[deps]`,
`[source]`, `[build]`, `[[artifact]]`; `bench` needs at least one `[[bench]]`. `[flash]` is required when an
artifact is flashable, and `[verify]` when a flashable artifact has a descriptor.

## README and bench_check headings

Checked by `./fw test` (case-insensitive, any `#` level; spec R20, R21):

- `README.md`: Purpose, Status, Build, Test, Flash, Verify, Bench check, Layout, Changes vs TI, Known limits.
  Status is one line quoting `./fw list`. The README contains no `20YY-MM-DD` date: bench dates live only in `[[bench]]`.
- `docs/bench_check.md`: Preconditions, Board state, Steps, Expected output, Pass/fail, Results log.

## Build state

`./fw list` (and `publish`) judge `build/build_info.json` in this order:

1. `none`: no record. If only the old `build/build_info.txt` exists: `stale`.
2. `stale`: an artifact is missing or its sha256 differs from the record; or `projects/<p>`, `fw` or `tools` changed
   in git between the recorded commit and HEAD; or git cannot answer.
3. `dirty`: the record says it was built dirty, or `git status --porcelain -- projects/<p> fw tools` is not empty.
4. `clean`.

Edits confined to `[[bench]]` tables and the `status` line of `project.toml` do not count as changes in steps 2
and 3; any other text change does, even a comment. The shown **status** is downgraded without evidence: `built` or
`bench` with no record shows `source`; `bench` with no `pass` `[[bench]]` whose `sha256` equals the current
default artifact shows `built`.

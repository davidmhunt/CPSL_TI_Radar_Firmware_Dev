# 2. Build a project

Prerequisite: [the build environment](01_build_env.md). Run from `firmware_dev/`.

```bash
./fw list                                  # pick a project
./fw build ti_stock_demos --dry-run        # deps check + the container command, builds nothing
./fw build ti_stock_demos                  # real build
```

`fw build` checks `./fw deps`, runs the project's `build.sh` in the container as your user, and writes
`projects/<project>/build/`. Every build starts from fresh staging; there are no incremental builds.

## What you get

- The project's artifacts (for example `ti_stock_demos`: `iwr1843_demo.bin`, `iwr6843_demo.bin` plus `.elf`
  files; cascade: `am273x_cascade.appimage`).
- `build/build_info.json`: firmware_dev commit (`-dirty` if the tree had changes), build config, and the
  sha256 of every artifact. `fw list` reads it to show the build state: `clean`, `dirty`, `stale` or `none`.
  Editing anything under the project, `fw` or `tools/` makes a build `dirty` (uncommitted) or `stale`
  (committed after the build); only edits to `[[bench]]` tables and the `status` line of `project.toml` do not.

## Options

| Option | Effect |
|--------|--------|
| `--variant V` | Build one variant, for projects that declare `variants` (`ti_stock_demos`: `18xx`, `68xx`) |
| `CCS_CONFIG=Debug ./fw build ...` | Debug build (cascade). `CCS_CONFIG` and any `FW_*` variable pass into the container |
| `./fw build <project> <arg>` | Extra arguments go to `build.sh` after the variant. Only ti_stock_demos reads one (a family, same as `--variant`); there is no `clean`: every build starts from scratch |

## Long builds

A cascade build takes about 15 minutes. Detach it so closing the terminal does not kill it:

```bash
setsid nohup ./fw build awr2243_cascade_ddm > build.log 2>&1 &
```

## Same path, same hash

Compilers embed the in-container build path in the binaries, so two builds give the same hash only for the
same project folder name. Do not compare hashes across renamed folders.

## Next

Hardware-free checks: `./fw test <project>`. Then [3. Flash and verify](03_flash_verify.md).

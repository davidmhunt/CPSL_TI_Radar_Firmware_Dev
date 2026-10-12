# 2. Build a project

You need the [build environment](01_build_env.md). Run all commands from `firmware_dev/`.

## 1. Pick a project and build it

```bash
./fw list
./fw build ti_stock_demos --dry-run
./fw build ti_stock_demos
```

`--dry-run` checks your setup and prints the command; it builds nothing. The last command is the real
build and always starts from scratch. It checks the installers itself, so `./fw deps` is not required first.

## 2. See what you got

The build writes `projects/<project>/build/`. It holds:

- The firmware files. For example `ti_stock_demos` gives `iwr1843_demo.bin` and `iwr6843_demo.bin`.
  The cascade gives `am273x_cascade.appimage`.
- `build_info.json`, which records the source version and a checksum of each file.

`./fw list` reads that record and shows the build state: `clean`, `dirty`, `stale` or `none`. Only a
`clean` build can be published. What the states mean: [build state](../reference/projects.md#build-state).

## Options

| Option | Effect |
|--------|--------|
| `--variant V` | Build one variant (a named build option). `ti_stock_demos` has `18xx` and `68xx`. |
| `CCS_CONFIG=Debug ./fw build ...` | Debug build (cascade only). |

## Long builds

The cascade build takes about 15 minutes. Start it like this so closing the terminal does not stop it:

```bash
setsid nohup ./fw build awr2243_cascade_ddm > build.log 2>&1 &
```

## Next

`./fw test <project>` runs checks on the project's files that need no board. Then [3. Flash and verify](03_flash_verify.md).

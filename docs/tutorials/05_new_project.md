# 5. Add a new project

This makes a project called `zz_demo`. Use your own name (lowercase letters, digits and `_`).
Run all commands from `firmware_dev/`. To only try it out, skip the git commands.

## 1. Create it

```bash
./fw new zz_demo
./fw list
```

`projects/zz_demo/` is a copy of [`_template/`](../../projects/_template/README.md), with status `stub`.
The comments in `project.toml` say which tables each status needs.

## 2. Commit the TI source unchanged

Copy the TI source you start from into `src/`, and commit it before you change anything. Every later change
is then a diff against that commit. The TI sources are in the image under `/opt/ti`:

```bash
docker compose run --rm --user "$(id -u):$(id -g)" firmware-env \
    cp -r /opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti/demo/xwr18xx/mmw/. \
    /build_context/projects/zz_demo/src/
git add projects/zz_demo
git commit -m "Firmware: add zz_demo TI baseline, unmodified source"
git rev-parse --short HEAD
```

The last command prints the commit hash for step 3. Commit source only, never TI binaries. Delete
generated files (`.bin`, `.map`) from `src/` first and note in the README what you removed.

## 3. Fill in `project.toml`

Replace every `TODO`:

- `[project]`: a summary and the status.
- `[deps]`: the SDK, its version, the toolchain and the `download_items`.
- `[source]`: `baseline` (the path you copied) and `baseline_commit` (the hash from step 2).
- `[build]`, `[flash]` and one `[[artifact]]` per output file.

An artifact's `descriptor` names a file in `CPSL_TI_Radar_cpp/config/firmware/`; copy an existing one.
All keys: [project reference](../reference/projects.md).

## 4. Write `build.sh`

Replace its `TODO` block. It must write only to `build/`. Then run:

```bash
./fw build zz_demo && ls -l projects/zz_demo/build/
```

## 5. Write `flash.sh`, tests and the bench check

1. If the board has a command-line flasher, call it in `flash.sh`. Exit `0` only on its success line.
   Otherwise keep the template, which prints the manual steps and exits `3`.
2. Add host tests in `tests/` and fill in `docs/bench_check.md`.
3. Run the checks with `./fw test zz_demo`.

## 6. Write the README and commit

Fill in `projects/zz_demo/README.md`. Follow its notes and keep its `##` headings. No dates.

```bash
git add projects/zz_demo
git commit -m "Firmware: zz_demo build and flash scripts"
```

Then [build](02_build.md), [flash and verify](03_flash_verify.md) and [publish](04_publish.md) as usual.
To delete the example: `rm -rf projects/zz_demo`.

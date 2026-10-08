# 5. Add a new project

This creates `zz_demo`; use your own name (lowercase, digits, `_`). To only try it out, skip the `git`
lines and delete the folder at the end. Run from `firmware_dev/`.

## 1. Create it

```bash
./fw new zz_demo            # copies projects/_template/ to projects/zz_demo/; refuses if it exists
./fw list                   # zz_demo appears with status stub
```

The folder holds `project.toml` (the manifest), `build.sh`, `flash.sh`, `README.md`, `src/`, `configs/`,
`docs/bench_check.md`, `tools/`, `tests/`. The comments in `project.toml` list which tables each status
needs (`stub`, `source`, `built`, `bench`).

## 2. Commit the TI baseline unmodified

Copy the TI source you start from into `src/` and commit it before changing anything, so every later change
is a diff against that commit. TI sources live in the image under `/opt/ti`:

```bash
docker compose run --rm --user "$(id -u):$(id -g)" firmware-env \
    cp -r /opt/ti/mmwave_sdk_03_06_02_00-LTS/packages/ti/demo/xwr18xx/mmw/. \
    /build_context/projects/zz_demo/src/
cd projects/zz_demo/src && rm -rf docs/doxygen *.bin *.map *.xer4f *.xe674 *.rov.xs && cd ../../..
git add projects/zz_demo
git commit -m "Firmware: add zz_demo TI baseline, unmodified source (xwr18xx/mmw, SDK 3.6.2)"
git rev-parse --short HEAD  # this is baseline_commit
```

Only source goes in git; never commit TI binaries. Record in the README what you deleted. (The `rm` list is
for this mmw example.)

## 3. Fill in `project.toml`

Replace every `TODO`: `[project]` summary and status, `[deps]` (sdk, sdk_version, toolchain,
`download_items`), `[source]` (`baseline` = the path copied in step 2, `baseline_commit` = the hash above),
`[build]`, one `[[artifact]]` per output (`file`, `board`, `descriptor`, `flashable`) and `[flash]`.
`status` rises as evidence appears: `stub`, `source`, `built`, `bench`.

## 4. Write `build.sh`

Replace the `TODO` block. It reads the `FW_*` environment, works from its own folder, and writes only to
`build/`. TI makefiles write objects next to sources, so copy `src/` into `build/` and build there; do not
call `git` (the container cannot see it). For an SDK 3.x demo:

```bash
pushd "${MMWAVE_SDK_PATH}/packages/scripts/unix" > /dev/null
set +u; source ./setenv.sh; set -u     # setenv.sh must be sourced from its own folder
popd > /dev/null
export MMWAVE_SDK_DEVICE=iwr18xx       # after setenv.sh, which sets its own default
rm -rf build/src && cp -r src build/src
make -C build/src all
cp build/src/xwr18xx_mmw_demo.bin build/
```

```bash
./fw build zz_demo && ls -l projects/zz_demo/build/    # artifacts + build_info.json
```

## 5. Write `flash.sh`, tests, and the bench check

If the board has a command-line flasher, call it in the `TODO` block and exit `0` only on its success
marker; otherwise keep the template: print the manual steps and exit `3`. Add hardware-free host tests in
`tests/` and complete `docs/bench_check.md` (headings: Preconditions, Board state, Steps, Expected output,
Pass/fail, Results log). Then:

```bash
./fw test zz_demo           # manifest, descriptors, cfgs, [test].commands, build --dry-run
```

## 6. Document and commit

Fill in the project `README.md`; keep exactly the template's `##` headings. Status is one line quoting
`./fw list`, with no dates (bench evidence lives in `project.toml`).

```bash
git add projects/zz_demo && git commit -m "Firmware: zz_demo build and flash scripts"
```

Then [build](02_build.md), [flash and verify](03_flash_verify.md) and [publish](04_publish.md) as usual.
To throw the example away: `rm -rf projects/zz_demo`.

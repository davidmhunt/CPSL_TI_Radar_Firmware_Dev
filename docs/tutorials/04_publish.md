# 4. Publish to shipped_firmware/

Publishing copies a clean build's flashable images into the parent repo's
[`shipped_firmware/`](../../../shipped_firmware/README.md) (parent repo), with
provenance, so users who never build can flash them. Run from `firmware_dev/`.

```bash
./fw publish <project> --dry-run      # lists what would be copied and where; writes nothing
./fw publish <project>                # copies to ../shipped_firmware/<BOARD>/<descriptor>/
```

## What it requires

`publish` refuses (exit 4) unless the build is **clean**: not `dirty` (uncommitted changes), not `stale`
(commits since the build), not `none`, and the artifacts still match `build/build_info.json`. Fix it by
committing your changes and rebuilding; check with `./fw list`. That is the whole gate: it does not require
a flash, a `verify` or a `bench` status, so run the bench check first. Only artifacts that are `flashable`
*and* have a non-empty `descriptor` are published; the rest are skipped.

## What it writes

For each published artifact: the image under `shipped_firmware/<BOARD>/<descriptor>/`, plus `<image>.provenance.json`: project, firmware_dev
commit, variant, toolchain, build time, image sha256, and the path and sha256 of every tracked chirp cfg
of the project (the `[test].cfgs` globs). Commit the image and its provenance together in the parent repo. `--dest D` publishes
to another folder (for testing; the layout under it is the same).

`<BOARD>` is the artifact's `board` field in `project.toml` (for example `IWR1843`). The *descriptor* is
the artifact's `descriptor` field: the id of a firmware description file the host driver reads,
`<descriptor>.json` in [`CPSL_TI_Radar_cpp/config/firmware/`](../../../CPSL_TI_Radar_cpp/config/firmware/)
(parent repo). It also supplies the probes `fw verify` sends.

## After publishing

Order matters: commit your `firmware_dev` changes, `./fw build`, then `./fw publish`. The descriptor
`CPSL_TI_Radar_cpp/config/firmware/<descriptor>.json` must already exist in the parent repo (publish does not
check; the host driver and `fw verify` need it). Then, in the parent repo:

1. Add the image's row to `shipped_firmware/README.md`.
2. Commit the images, their provenance, that row and the moved `firmware_dev` pointer (`git add firmware_dev`).
   Pushing is up to you.

TI-derived binaries may need a licensing decision before a public release; see
[`docs/firmware.md`](../../../docs/firmware.md) (parent repo), "Open v2.0 items".

## Next

[5. Add a project](05_new_project.md).

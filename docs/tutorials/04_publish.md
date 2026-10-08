# 4. Publish to shipped_firmware/

Publishing copies a verified build's flashable images into the parent repo's `shipped_firmware/`, with
provenance, so users who never build can flash them. Run from `firmware_dev/`.

```bash
./fw publish <project> --dry-run      # lists what would be copied and where; writes nothing
./fw publish <project>                # copies to ../shipped_firmware/<BOARD>/<descriptor>/
```

## What it requires

`publish` refuses (exit 4) unless the build is **clean**: not `dirty` (uncommitted changes), not `stale`
(commits since the build), not `none`, and the artifacts still match `build/build_info.json`. Fix it by
committing your changes and rebuilding; check with `./fw list`.

## What it writes

For each flashable artifact: the image under `shipped_firmware/<BOARD>/<descriptor>/` (the descriptor is
the id from `CPSL_TI_Radar_cpp/config/firmware/`), plus `<image>.provenance.json`: project, firmware_dev
commit, variant, toolchain, build time, image sha256, and the path and sha256 of every tracked chirp cfg
of the project. Commit the image and its provenance together in the parent repo. `--dest D` publishes
somewhere else (for testing).

## After publishing

1. Add or check the image's row in `shipped_firmware/README.md`.
2. Confirm the descriptor's name matches a `CPSL_TI_Radar_cpp/config/firmware/<descriptor>.json`.
3. Commit in the parent repo. Pushing is up to you.

TI-derived binaries may need a licensing decision before a public release; see `docs/firmware.md` in the
parent repo ("Open v2.0 items").

## Next

[5. Add a project](05_new_project.md).

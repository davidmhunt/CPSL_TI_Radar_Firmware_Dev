# 4. Publish to shipped_firmware/

Publishing copies a clean build into the parent repo's
[`shipped_firmware/`](../../../shipped_firmware/README.md) folder, with a record of where it came from.
Lab users who never build can then flash it. Run all commands from `firmware_dev/`.

## 1. Get a clean build

1. Run the bench check first. Publishing does not check that the firmware was ever flashed.
2. Commit your changes in `firmware_dev`.
3. Build again:

   ```bash
   ./fw build <project>
   ./fw list
   ```

4. `./fw list` must show the build as `clean`. If it says `dirty` or `stale`, commit and build again.

## 2. Publish

```bash
./fw publish <project> --dry-run
./fw publish <project>
```

1. `--dry-run` lists what would be copied and writes nothing.
2. The real command copies each image to `../shipped_firmware/<BOARD>/<descriptor>/`.
3. Next to each image it writes `<image>.provenance.json`, the record of where the image came from
   (fields: [`fw` reference](../reference/fw_cli.md#verbs)).

`<BOARD>` and `<descriptor>` come from the project's `project.toml`. The descriptor (a JSON file that tells
the driver how to use the image) must already exist in [`CPSL_TI_Radar_cpp/config/firmware/`](../../../CPSL_TI_Radar_cpp/config/firmware/).
Use `--dest D` to try it in another folder. All rules and exit codes: [`fw` reference](../reference/fw_cli.md#verbs).

## 3. Record it in the parent repo

1. Add a row for the image to `shipped_firmware/README.md`.
2. Commit the images, their provenance files, the new row and the moved `firmware_dev` pointer:

   ```bash
   git add shipped_firmware firmware_dev
   ```

Pushing is up to you. TI-derived binaries may need a licence check before a public release:
see "Open v2.0 items" in [`docs/firmware.md`](../../../docs/firmware.md).

## Next

[5. Add a project](05_new_project.md).

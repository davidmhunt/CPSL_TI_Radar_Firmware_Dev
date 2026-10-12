# 3. Flash and verify

You need a [build](02_build.md) and a person at the bench. Run all commands from `firmware_dev/`.

## Before you start

1. Make sure nobody else is using the board or its serial ports. Ask the lab.
2. Read the **Flash** section of your project README. It gives the switch or jumper settings and the
   power steps. Change jumpers only with the power off.
3. Find the board's port with `./fw ports`. Use the one ending in `-if00`.

## Flash

1. Preview first. This touches no board:

   ```bash
   ./fw flash <project> /dev/serial/by-id/<...>-if00 --dry-run
   ```

2. Put the board in flash mode. Power-cycle it fully (USB and supply). Do this before every attempt,
   including retries. A retry without a power-cycle fails.
3. Run the real flash:

   ```bash
   ./fw flash <project> /dev/serial/by-id/<...>-if00
   ```

4. Read the checklist it prints. When asked, type `FLASH MODE CONFIRMED` and press Enter.
5. Wait for the success line:
   - IWR1843 and IWR6843: `SUCCESS!! File type META_IMAGE1`
   - Cascade: `All commands from config file are executed !!!`
6. Put the board back in run mode (see the project README) and power-cycle it.

Exit code 3 means there is no automatic flasher for this image; follow the manual steps it prints.
Flashing from a script: [`fw` reference](../reference/fw_cli.md#scripted-flashing---plan----confirm).

## Verify

```bash
./fw verify <project> --port /dev/serial/by-id/<...>-if00
```

This asks the board which firmware it runs and prints `pass` or `fail`. On the cascade it prints
`skipped` and sends nothing, because that board accepts only one chirp config per power-up
([why](../reference/fw_cli.md#verbs)). To check the cascade, follow its `docs/bench_check.md`, then
power-cycle (12 V).

## Record a bench check

After a person runs a project's `docs/bench_check.md` on a real board, add this to its `project.toml`:

```toml
[[bench]]
board = "IWR1843"
date = "2026-10-07"
sha256 = "<checksum of the image you flashed>"
result = "pass"            # pass, partial or fail
doc = "docs/bench_check.md"
```

Get the checksum from `./fw flash --dry-run` or `build/build_info.json`. Commit the entry in
`firmware_dev`. How a bench record changes the status shown by `./fw list`: [project reference](../reference/projects.md#build-state).

## Next

[4. Publish](04_publish.md).

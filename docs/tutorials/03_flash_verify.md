# 3. Flash and verify

Prerequisite: a [build](02_build.md). This step needs a person at the bench: it changes jumpers, power and
a board that only one user can hold. Run from `firmware_dev/`.

## Before you start

- The board is yours alone: no other terminal, viewer or script may hold its serial ports.
- Know your project's flash mode: the project README's Flash section and `docs/bench_check.md` give the exact
  jumpers. IWR boards use the SOP switches (flash mode SOP 101, functional mode SOP 001). The cascade EVM
  uses jumper J6 (bottom two pins = UART flash mode, top two = QSPI run mode); 12 V is needed for USB to
  enumerate. Change jumpers only with power off.
- Find the CLI port: `./fw ports` lists the by-id serial ports and whether a process holds them. Use the
  `-if00` port.

## Flash

```bash
./fw flash <project> /dev/serial/by-id/<...>-if00 --dry-run   # checklist, command and image sha256; touches no board
./fw flash <project> /dev/serial/by-id/<...>-if00             # the real flash
```

1. Put the board in flash mode and **power-cycle fully (USB and supply) before every attempt, retries
   included**. A retry without a power-cycle fails (`unexpected data`).
2. `fw flash` prints the checklist and asks you to type `FLASH MODE CONFIRMED` at a terminal. A wrong phrase,
   or no terminal, exits 5. A non-by-id port also needs the typed phrase `USE THIS PORT`.
3. Success is the flasher's own marker (`SUCCESS!! File type META_IMAGE1` for IWR1843; `All commands from
   config file are executed !!!` for the cascade). Exit 3 means no headless flasher: the manual steps are
   printed instead (the IWR6843 image).
4. Return to functional mode (SOP 001, or J6 top two pins) and power-cycle.

Scripts and agents cannot type the phrase: they run `./fw flash <project> <port> --plan`, show the checklist
to a human, and run `--confirm <token>` only after the human has done the bench steps.

## Verify

```bash
./fw verify <project> --port /dev/serial/by-id/<...>-if00
```

`verify` sends the descriptor's identify probes (for example `version`) and never a chirp cfg, and reports
the firmware found. Cascade firmware cannot be probed (a probe would use its one cfg), so `verify` only
listens there and says `skipped`.

## A cfg is accepted once per power-up

Any step that sends a chirp cfg (the bench checks, the host driver) needs a fresh power-cycle first.

## Bench check

The full user-run check is in each project's `docs/bench_check.md`. A passing run is recorded as a
`[[bench]]` entry in `project.toml` (board, date, image sha256, result, doc).

## Next

[4. Publish](04_publish.md).

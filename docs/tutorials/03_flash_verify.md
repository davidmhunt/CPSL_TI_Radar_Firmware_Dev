# 3. Flash and verify

Prerequisite: a [build](02_build.md). This step needs a person at the bench: it changes jumpers, power and
a board that only one user can hold. Run from `firmware_dev/`.

## Before you start

- The board is yours alone: no other terminal, viewer or script may hold its serial ports, and
  nobody else in the lab may be using the board (agree this before flashing).
- Know your project's flash mode: the project README's Flash section and
  `projects/<project>/docs/bench_check.md` give the exact switch and jumper settings. Change jumpers only with power off.
  - IWR boards use the SOP switches (S1), read as three bits in the order SOP2 SOP1 SOP0, 1 = closed. Flash mode
    is "SOP 101" (SOP0 and SOP2 closed, SOP1 open); functional mode is "SOP 001" (SOP0 only).
  - The cascade EVM uses jumper J6 (bottom two pins = UART flash mode, top two = QSPI run mode).
  - Power: IWR boards run from USB plus a 5 V supply; the cascade EVM needs its 12 V supply, and its USB
    only enumerates with 12 V on.
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
   or no terminal, exits 5. Use a by-id `-if00` port. At a terminal any other port (such as `/dev/ttyACM0`)
   is accepted only after you also type `USE THIS PORT`; under `--json`, `--plan` and `--confirm` it is refused.
3. Success is the flasher's own marker (`SUCCESS!! File type META_IMAGE1` for IWR1843; `All commands from
   config file are executed !!!` for the cascade). Exit 3 means no headless flasher: the manual steps are
   printed instead (the IWR6843 image).
4. Return to functional mode (SOP 001, or J6 top two pins) and power-cycle.

Scripts and agents cannot type the phrase: they run `./fw flash <project> <port> --plan`, show the checklist
to a human, and run `./fw flash <project> <port> --confirm <token>` only after the human has done the bench
steps. The token is a random string that `--plan` prints (and returns as `token` in its JSON result). It
lasts 300 s, is bound to that project, port and image sha256 (`--confirm` refuses if any differ), and is
single use, even when the confirm then fails. A new `--plan` for the same port invalidates the earlier
token. `--confirm` still re-checks that the port exists, is free and is by-id.

## Verify

```bash
./fw verify <project> --port /dev/serial/by-id/<...>-if00
```

`verify` sends the descriptor's identify probes (for example `version`) and never a chirp cfg, and reports
the firmware found (`outcome` `pass` or `fail`). **On the cascade it reports `skipped` and sends nothing**:
a probe would use up the board's one cfg. To check cascade firmware, run the project's manual check (its
`docs/bench_check.md`); that check sends a cfg, so it uses up the allowance and you **power-cycle (12 V)
afterwards**.

## A cfg is accepted once per power-up (cascade only)

On the cascade, any step that sends a chirp cfg (the bench check, the host driver) needs a fresh power-cycle
first. IWR boards (including the SAR image) accept a cfg again without one; they still need one power-cycle
after flashing.

## Bench check and the `[[bench]]` record

The full user-run check is in each project's `docs/bench_check.md`. After you run it on a real board, add
one `[[bench]]` table to the project's `project.toml`:

```toml
[[bench]]
board = "IWR1843"
date = "2026-10-07"
sha256 = "<sha256 of the image you flashed, from ./fw flash --dry-run or build_info.json>"
result = "pass"            # pass | partial | fail
doc = "docs/bench_check.md"
```

Add it only for a check a person ran, and commit it in `firmware_dev` (it does not make the build dirty or stale);
the sha256 ties the result to that exact image. Longer notes go in the project's `docs/`; measured numbers in
the parent repo's `docs/RESULTS.md`. A rebuild that changes the image's sha256 drops `bench` back to `built`
until a new `pass` record matches. `status = "bench"` in
`[project]` needs a `pass` record whose sha256 equals the current build's default artifact; otherwise
`./fw list` reports `built`. Editing `[[bench]]` tables or the `status` line does not make the build `dirty`
or `stale`; any other text change in the project does (see [tutorial 2](02_build.md)).

## Next

[4. Publish](04_publish.md).

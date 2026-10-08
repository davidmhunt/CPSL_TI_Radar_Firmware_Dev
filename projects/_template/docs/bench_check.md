# PROJECT_NAME bench check

Run by a person at the bench; not run by `./fw test`. Record the outcome in `project.toml` as a `[[bench]]` entry.

## Preconditions

- Board, cables and serial ports free (claim them in `status.md`); image built with `./fw build PROJECT_NAME`.

## Board state

- Boot-mode jumpers and power state before each step. A cfg is accepted once per power-up on some firmware: power-cycle first.

## Steps

1. TODO flash with `./fw flash PROJECT_NAME <by-id port>`.
2. TODO restore run mode, power-cycle, run `./fw verify PROJECT_NAME --port <by-id port>`.

## Expected output

- TODO the flasher success line, the CLI banner, the data stream.

## Pass/fail

- Pass when every expected output above is seen; anything else is fail or partial (say which part).

## Results log

Results are recorded in `project.toml` `[[bench]]` (board, date, image sha256, result, this doc).

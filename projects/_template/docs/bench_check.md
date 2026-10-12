# PROJECT_NAME bench check

A person runs this at the bench. `./fw test` does not. Each section: a few lines and the exact commands.

## Preconditions

_What must be true first, for example: board free, image built with `./fw build PROJECT_NAME`._

## Board state

_Jumper or switch settings and power state. Say if the board needs a power-cycle before a step._

## Steps

_Numbered steps with commands. Typically:_

1. `./fw flash PROJECT_NAME <by-id port>`
2. Return to run mode, power-cycle, then `./fw verify PROJECT_NAME --port <by-id port>`

## Expected output

_The exact text to look for: flasher success line, CLI banner, data stream._

## Pass/fail

Pass when every expected output above appears. Anything else is `fail` or `partial`: say which part.

## Results log

Keep no results here. Add a `[[bench]]` entry to `project.toml` and commit it in `firmware_dev`.

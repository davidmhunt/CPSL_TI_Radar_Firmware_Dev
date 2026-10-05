# PROJECT_NAME

One sentence: what this firmware does and which board it runs on.

## What changed vs TI

The TI baseline is recorded in `project.env` (`BASELINE`, `BASELINE_COMMIT`).
List every change made on top of it, newest last, so `git diff <BASELINE_COMMIT> -- src/`
reads as expected:

- _(none yet: `src/` is the unmodified TI baseline)_

## Status

What works, what was tested on which board and when, known issues.

- Build: not yet
- On-board: not yet

## Build, flash, run

From `firmware_dev/` (see `projects/README.md` for prerequisites):

```bash
./fw build PROJECT_NAME                 # outputs in projects/PROJECT_NAME/build/
./fw flash PROJECT_NAME /dev/ttyACM0    # board-specific steps below
```

Board setup (jumpers, power, boot mode) and how to check it runs:

- _TODO_

#!/bin/bash
# Flash this project's image. Runs INSIDE the firmware container (the `flash` service, which passes
# the host's /dev through); start it from firmware_dev/ with
#     ./fw flash <project> <port> [image] [--dry-run]
# fw runs the safety gates on the host first. Standard environment (set by fw): FW_PROJECT, FW_PORT,
# FW_IMAGE (container path of the image), FW_DRY_RUN (1 = print the command, flash nothing). The same
# values arrive as arguments: flash.sh [--dry-run] <port> <image>.
#
# Exit codes (projects/README.md): 0 flashed and the flasher confirmed success, 1 flashing failed,
# 2 bad arguments, 3 no headless flasher for this board (manual steps printed instead).
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

DRY="${FW_DRY_RUN:-0}"; POS=()
for a in "$@"; do
    case "$a" in
        -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2 ;;
        --dry-run) DRY=1 ;;
        *) POS+=("$a") ;;
    esac
done
PORT="${POS[0]:-${FW_PORT:-}}"
IMAGE="${POS[1]:-${FW_IMAGE:-}}"
[[ -n "$PORT" && -n "$IMAGE" ]] || { echo "usage: flash.sh [--dry-run] <port> <image>" >&2; exit 2; }
[[ -f "$IMAGE" ]] || { echo "ERROR: image not found: ${IMAGE} (run ./fw build first?)" >&2; exit 1; }

# ---- TODO: if the board has a headless flasher, call it here (honour $DRY) and exit 0 only on
# confirmed success (match the success_marker of project.toml). Otherwise keep this: print the
# manual steps and exit 3 (never pretend the board was flashed).
HOST_IMAGE="${IMAGE#/build_context/}"   # path as the user sees it from firmware_dev/
cat <<EOT
No headless flasher for this board: flash it by hand.
  1. Power off the board, set it to flashing mode (jumpers), power on.
  2. Open the TI flashing tool on the host, pick the board's serial port (${PORT}).
  3. Flash: ${HOST_IMAGE}
  4. Power off, set functional mode, power on.
EOT
exit 3

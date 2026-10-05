#!/bin/bash
# Flash this project's image. Runs INSIDE the firmware container (the `flash` service, which
# passes the host's /dev through); start it from firmware_dev/ with
#     ./fw flash <project> <port> [image]
# <port>   serial port of the board, e.g. /dev/ttyACM0 or /dev/serial/by-id/...
# [image]  image to flash; default: the first file in ARTIFACTS, under ./build/
#
# Exit codes (projects/README.md, "flash.sh exit codes"):
#   0  image flashed and the flasher confirmed success
#   1  flashing failed
#   2  bad arguments
#   3  no headless flasher for this board: manual steps were printed instead
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
source ./project.env

usage() { sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }
[[ $# -ge 1 && $# -le 2 ]] || usage
[[ "$1" == "-h" || "$1" == "--help" ]] && usage

PORT="$1"
read -r FIRST_ARTIFACT _ <<<"${ARTIFACTS}"
IMAGE="$(realpath -m "${2:-build/${FIRST_ARTIFACT}}")"
[[ -f "${IMAGE}" ]] || { echo "ERROR: image not found: ${IMAGE} (run ./fw build first?)" >&2; exit 1; }

# ---- TODO: if the board has a headless flasher, call it here and exit 0 on confirmed success.
# Otherwise keep this: print the manual steps and exit 3 (never pretend the board was flashed).
HOST_IMAGE="${IMAGE#/build_context/}"   # path as the user sees it from firmware_dev/
cat <<EOF
No headless flasher for ${BOARD}: flash it by hand.
  1. Power off the board, set it to flashing mode (SOP jumpers), power on.
  2. Open TI UniFlash on the host, pick the board's serial port (${PORT}).
  3. Flash: ${HOST_IMAGE}
  4. Power off, set functional mode, power on.
EOF
exit 3

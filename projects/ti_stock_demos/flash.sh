#!/bin/bash
# Flash an SDK 3.6 demo image. Runs INSIDE the firmware container (the `flash` service);
#     ./fw flash ti_stock_demos <port> [image]
# Exit codes: 0 flashed, 1 failed, 2 bad arguments, 3 manual steps printed.
# There is no Linux command-line flasher for IWR1843/IWR6843 in the image (TI's tool is the GUI
# UniFlash), so this prints the manual steps and exits 3.
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
source ./project.env

usage() { sed -n '2,6p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }
[[ $# -ge 1 && $# -le 2 ]] || usage
[[ "$1" == "-h" || "$1" == "--help" ]] && usage

PORT="$1"
read -r FIRST_ARTIFACT _ <<<"${ARTIFACTS}"
IMAGE="$(realpath -m "${2:-build/${FIRST_ARTIFACT}}")"
[[ -f "${IMAGE}" ]] || { echo "ERROR: image not found: ${IMAGE} (run ./fw build ti_stock_demos first?)" >&2; exit 1; }

HOST_IMAGE="${IMAGE#/build_context/}"
cat <<EOF2
No headless flasher for ${BOARD}: flash it by hand.
  1. Power off the board; set the SOP switches to flashing mode (see the board user guide:
     IWR1843BOOST SOP0+SOP2, IWR6843ISK SOP0+SOP2); power on.
  2. Open TI UniFlash on the host, choose the board, serial port ${PORT}, Format = "bin".
  3. Flash ${HOST_IMAGE}  (iwr1843_demo.bin for IWR1843, iwr6843_demo.bin for IWR6843).
  4. Power off, set functional mode (SOP0 only), power on.
EOF2
exit 3

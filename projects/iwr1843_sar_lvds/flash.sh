#!/bin/bash
# Flash this project's IWR1843 image. Runs INSIDE the firmware container (the `flash` service);
#     ./fw flash iwr1843_sar_lvds <port> [image]
# Exit codes: 0 flashed, 1 failed, 2 bad arguments, 3 manual steps printed.
# There is no verifiable headless Linux flasher for SDK 3 xWR18xx in the image: CCS's DSLite only
# ships serial-flash targets for AM2xx (no xWR18xx serial target), and TI's documented path is the
# UniFlash GUI. So this prints the manual steps and exits 3.
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
source ./project.env

usage() { sed -n '2,7p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }
[[ $# -ge 1 && $# -le 2 ]] || usage
[[ "$1" == "-h" || "$1" == "--help" ]] && usage

PORT="$1"
read -r FIRST_ARTIFACT _ <<<"${ARTIFACTS}"
IMAGE="$(realpath -m "${2:-build/${FIRST_ARTIFACT}}")"
[[ -f "${IMAGE}" ]] || { echo "ERROR: image not found: ${IMAGE} (run ./fw build iwr1843_sar_lvds first?)" >&2; exit 1; }

HOST_IMAGE="${IMAGE#/build_context/}"
cat <<EOF
No headless flasher for ${BOARD}: flash it by hand.
  1. Power off the board; set the SOP jumpers to flashing mode (IWR1843BOOST: SOP0 + SOP2 closed);
     power on.
  2. Open TI UniFlash on the host, choose IWR1843 (serial), COM port = the board's CLI/UART port
     (${PORT}; usually the lower-numbered XDS110 port), Format = "bin".
  3. Flash ${HOST_IMAGE} as Meta Image 1 and wait for "Flash operation completed".
  4. Power off, set functional mode (SOP0 only closed), power on.
  5. Send a configs/*.cfg over the CLI port at 115200 baud (one cfg per power-up).
EOF
exit 3

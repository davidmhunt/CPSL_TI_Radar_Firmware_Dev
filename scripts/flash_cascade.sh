#!/bin/bash
# Flash the AM273x + AWR2243 2-chip cascade demo over the UART bootloader.
#
# Usage: flash_cascade.sh <serial_port> [appimage|prebuilt]
#   <serial_port>  Application/User UART of the EVM, e.g. /dev/ttyUSB0 or /dev/serial/by-id/...
#   appimage       Path to an .appimage (default: build/cascade/am273x_cascade.appimage)
#   prebuilt       Flash TI's prebuilt am273x_mmw_cascade_demo_DDM.appimage from the Radar Toolbox
#
# The board must be in UART boot mode (J6 jumper on the bottom two pins) and power-cycled first.
# Afterwards move J6 to the top two pins (QSPI boot) and power-cycle to run the demo.
#
# Works inside the firmware container (docker compose run --rm flash ...) or natively with
# TI_ROOT pointing at a host install containing the MCU+ SDK and Radar Toolbox.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TI_ROOT="${TI_ROOT:-/opt/ti}"
MCU_PLUS_SDK_PATH="${MCU_PLUS_SDK_PATH:-${TI_ROOT}/mcu_plus_sdk_am273x_08_05_00_24}"
RADAR_TOOLBOX_INSTALL_PATH="${RADAR_TOOLBOX_INSTALL_PATH:-${TI_ROOT}/radar_toolbox_4_00_00_05}"
PREBUILT_DIR="${REPO_ROOT}/firmware/cascade/src/demo/prebuilt_binaries"
TOOLBOX_APPIMAGE="${RADAR_TOOLBOX_INSTALL_PATH}/source/ti/examples/Automotive_ADAS_and_Parking/mmwave_2_chip_cascade/prebuilt_binaries/am273x_mmw_cascade_demo_DDM.appimage"
UNIFLASH="${MCU_PLUS_SDK_PATH}/tools/boot/uart_uniflash.py"

usage() {
    sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 1
}

[[ $# -ge 1 && $# -le 2 ]] || usage
[[ "$1" == "-h" || "$1" == "--help" ]] && usage

PORT="$1"
IMAGE_ARG="${2:-${REPO_ROOT}/build/cascade/am273x_cascade.appimage}"

if [[ "${IMAGE_ARG}" == "prebuilt" ]]; then
    APPIMAGE="${TOOLBOX_APPIMAGE}"
else
    APPIMAGE="$(realpath -m "${IMAGE_ARG}")"
fi

die() { echo "ERROR: $*" >&2; exit 1; }

[[ -e "${PORT}" ]] || die "serial port ${PORT} not found (check 'ls -l /dev/serial/by-id/' and that the device is passed into the container)"
[[ -f "${UNIFLASH}" ]] || die "uart_uniflash.py not found at ${UNIFLASH} (set TI_ROOT or MCU_PLUS_SDK_PATH)"
[[ -f "${APPIMAGE}" ]] || die "appimage not found: ${APPIMAGE}"
for f in sbl_uart_uniflash.release.tiimage sbl_qspi.release.tiimage; do
    [[ -f "${PREBUILT_DIR}/${f}" ]] || die "missing ${PREBUILT_DIR}/${f}"
done

# uart_uniflash.py resolves cfg paths relative to the cwd and writes temp files next to each
# input, so stage everything into a scratch dir and generate the cfg there.
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT
cp "${PREBUILT_DIR}/sbl_uart_uniflash.release.tiimage" "${PREBUILT_DIR}/sbl_qspi.release.tiimage" "${WORK_DIR}/"
cp "${APPIMAGE}" "${WORK_DIR}/app.appimage"
cat > "${WORK_DIR}/flash.cfg" <<'EOF'
--flash-writer=sbl_uart_uniflash.release.tiimage
--file=sbl_qspi.release.tiimage --operation=flash --flash-offset=0x0
--file=app.appimage --operation=flash --flash-offset=0xA0000
EOF

# The ROM bootloader sends 'C' continuously while it waits, and the EVM's XDS110 buffers them while the
# port is closed. uart_uniflash.py doesn't flush on open, so XMODEM reads that backlog as replies to block 1
# ("expected ACK; got b'C'") and aborts within a second. Run a patched copy that drops stale input first.
python3 - "${UNIFLASH}" "${WORK_DIR}/uart_uniflash.py" <<'EOF'
import sys
src, dst = sys.argv[1:]
anchor = "    ser = open_serial_port(serialport, baudrate)\n"
text = open(src).read()
assert text.count(anchor) == 1, "uart_uniflash.py changed; update the flush patch in flash_cascade.sh"
flush = ("    time.sleep(0.2)\n"
         "    if ser.in_waiting:\n"
         "        print('Discarding {} stale byte(s) from the bootloader'.format(ser.in_waiting))\n"
         "    ser.reset_input_buffer()\n")
open(dst, "w").write(text.replace(anchor, anchor + flush))
EOF
UNIFLASH="${WORK_DIR}/uart_uniflash.py"

echo "Port:     ${PORT}"
echo "Appimage: ${APPIMAGE} ($(stat -c %s "${APPIMAGE}") bytes)"
echo "Make sure J6 is on the bottom two pins (UART boot) and the board was power-cycled."
echo

# uart_uniflash.py calls sys.exit() (status 0) on most failures, so check its output instead.
LOG="${WORK_DIR}/uniflash.log"
(cd "${WORK_DIR}" && python3 "${UNIFLASH}" -p "${PORT}" --cfg=flash.cfg) 2>&1 | tee "${LOG}" || true

if grep -q "All commands from config file are executed" "${LOG}"; then
    echo
    echo "Flash OK. Move J6 to the top two pins (QSPI boot) and power-cycle the board."
else
    die "flashing did not complete. If this is a new board, see 'Possible Flashing Issues' (Quad Enable) in the cascade user guide."
fi

#!/bin/bash
# Flash the AM273x + AWR2243 2-chip cascade demo over the UART bootloader. Runs INSIDE the firmware container
# (the `flash` service, host /dev passed through); start it from firmware_dev/ with
#     ./fw flash awr2243_cascade_ddm <serial_port> [image|prebuilt] [--dry-run]
# fw runs the safety gates on the host first. Standard environment (set by fw): FW_PROJECT, FW_PORT,
# FW_IMAGE (container path of the image, or the keyword `prebuilt`), FW_DRY_RUN (1 = print the plan,
# open no port, flash nothing). The same values arrive as arguments: flash.sh [--dry-run] <port> <image>.
#   <serial_port>  CLI/Application UART of the EVM, /dev/serial/by-id/...-if00
#   image          Path to an .appimage (default: build/am273x_cascade.appimage in this project)
#   prebuilt       TI's prebuilt am273x_mmw_cascade_demo_DDM.appimage from the Radar Toolbox
#
# The board must be in UART boot mode (J6 jumper on the bottom two pins) and power-cycled first.
# Afterwards move J6 to the top two pins (QSPI boot) and power-cycle to run the demo.
#
# Exit codes (projects/README.md): 0 flashed and confirmed (or dry run ok), 1 failed, 2 bad arguments.
# Also works natively with TI_ROOT pointing at a host install containing the MCU+ SDK and Radar Toolbox.
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

TI_ROOT="${TI_ROOT:-/opt/ti}"
MCU_PLUS_SDK_PATH="${MCU_PLUS_SDK_PATH:-${TI_ROOT}/mcu_plus_sdk_am273x_08_05_00_24}"
RADAR_TOOLBOX_INSTALL_PATH="${RADAR_TOOLBOX_INSTALL_PATH:-${TI_ROOT}/radar_toolbox_4_00_00_05}"
PREBUILT_DIR="$PWD/prebuilt_binaries"
TOOLBOX_APPIMAGE="${RADAR_TOOLBOX_INSTALL_PATH}/source/ti/examples/Automotive_ADAS_and_Parking/mmwave_2_chip_cascade/prebuilt_binaries/am273x_mmw_cascade_demo_DDM.appimage"
UNIFLASH="${MCU_PLUS_SDK_PATH}/tools/boot/uart_uniflash.py"

usage() {
    sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 2
}

DRY="${FW_DRY_RUN:-0}"; POS=()
for a in "$@"; do
    case "$a" in
        -h|--help) usage ;;
        --dry-run) DRY=1 ;;
        *) POS+=("$a") ;;
    esac
done
PORT="${POS[0]:-${FW_PORT:-}}"
IMAGE_ARG="${POS[1]:-${FW_IMAGE:-$PWD/build/am273x_cascade.appimage}}"
[[ -n "$PORT" && ${#POS[@]} -le 2 ]] || usage

if [[ "${IMAGE_ARG}" == "prebuilt" ]]; then
    APPIMAGE="${TOOLBOX_APPIMAGE}"
else
    APPIMAGE="$(realpath -m "${IMAGE_ARG}")"
fi

die() { echo "ERROR: $*" >&2; exit 1; }

[[ "$DRY" == 1 || -e "${PORT}" ]] || die "serial port ${PORT} not found (check 'ls -l /dev/serial/by-id/' and that the device is passed into the container)"
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
assert text.count(anchor) == 1, "uart_uniflash.py changed; update the flush patch in flash.sh"
flush = ("    time.sleep(0.2)\n"
         "    if ser.in_waiting:\n"
         "        print('Discarding {} stale byte(s) from the bootloader'.format(ser.in_waiting))\n"
         "    ser.reset_input_buffer()\n")
open(dst, "w").write(text.replace(anchor, anchor + flush))
EOF
UNIFLASH="${WORK_DIR}/uart_uniflash.py"

echo "Port:     ${PORT}"
echo "Appimage: ${APPIMAGE} ($(stat -c %s "${APPIMAGE}") bytes)"
echo "sha256:   $(sha256sum "${APPIMAGE}" | cut -d' ' -f1)"
echo "Make sure J6 is on the bottom two pins (UART boot) and the board was power-cycled."
echo

if [[ "$DRY" == 1 ]]; then
    echo "DRY RUN: the flush patch applied; would run (cwd = scratch dir with the two SBL images + app.appimage):"
    echo "  python3 uart_uniflash.py -p ${PORT} --cfg=flash.cfg"
    sed 's/^/  flash.cfg: /' "${WORK_DIR}/flash.cfg"
    echo "Nothing flashed, port not opened."
    exit 0
fi

# uart_uniflash.py calls sys.exit() (status 0) on most failures, so check its output instead.
LOG="${WORK_DIR}/uniflash.log"
(cd "${WORK_DIR}" && python3 "${UNIFLASH}" -p "${PORT}" --cfg=flash.cfg) 2>&1 | tee "${LOG}" || true

if grep -q "All commands from config file are executed" "${LOG}"; then
    echo
    echo "Flash OK. Move J6 to the top two pins (QSPI boot) and power-cycle the board."
else
    die "flashing did not complete. If this is a new board, see 'Possible Flashing Issues' (Quad Enable) in the cascade user guide."
fi

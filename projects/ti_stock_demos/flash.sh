#!/bin/bash
# Flash the stock SDK 3.6 IWR1843 demo with UniFlash 9.6.0's DSLite (same flow as iwr1843_sar_lvds/flash.sh, which is bench-confirmed;
# this stock-demo flash is untested on the bench until firmware-19 Step 2). IWR1843 images only: an IWR6843 image is refused (exit 2).
# Runs INSIDE the firmware container (the `flash` service); do not run it by hand, use
#     ./fw flash ti_stock_demos <port> [image] [--dry-run]
# which first asks the human at the bench to confirm flash mode (SOP0+SOP2, power-cycled).
# IWR6843 (iwr6843_demo.bin): no ccxml/board here; flash it by hand with the GUI UniFlash (README).
# Exit codes: 0 flashed, 1 failed, 2 bad arguments, 3 DSLite missing in the image.
# --dry-run prints the exact DSLite command and the image sha256, touches no port, and exits 0.
# Success is only "SUCCESS!! File type META_IMAGE1" in the DSLite output; a trailing
# "Can't Run Target CPU" is accepted only AFTER that line (not seen in the confirmed run's log; unproven).
# Power-cycle (USB + 5 V) before EVERY attempt: a retry without it fails with "Received unexpected data".
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
source ./project.env

usage() { sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2; }

DRY=0; POS=()
for a in "$@"; do
    case "$a" in
        -h|--help) usage ;;
        --dry-run) DRY=1 ;;
        *) POS+=("$a") ;;
    esac
done
[[ ${#POS[@]} -ge 1 && ${#POS[@]} -le 2 ]] || usage

PORT="${POS[0]}"
read -r FIRST_ARTIFACT _ <<<"${ARTIFACTS}"
IMAGE="$(realpath -m "${POS[1]:-build/${FIRST_ARTIFACT}}")"
case "$(basename "$IMAGE")" in
    *6843*|*68xx*) echo "ERROR: refusing IWR6843 image ${IMAGE##*/}: headless flashing is IWR1843 only (no ccxml/board for 6843). Flash it by hand with UniFlash, see README.md." >&2; exit 2 ;;
esac
[[ -f "${IMAGE}" ]] || { echo "ERROR: image not found: ${IMAGE} (run ./fw build ti_stock_demos 18xx first?)" >&2; exit 1; }

export HOME="${HOME:-/tmp/fwhome}"; mkdir -p "$HOME"
CCXML="$PWD/configs/iwr1843_uniflash.ccxml"
[[ -f "$CCXML" ]] || { echo "ERROR: missing $CCXML" >&2; exit 1; }
# The DSLite binary is called directly: dslite.sh evals its flattened "$@" and loses argument quoting.
DSLITE="${UNIFLASH_PATH:-/opt/ti/uniflash_9.6.0}/deskdb/content/TICloudAgent/linux/ccs_base/DebugServer/bin/DSLite"
mkdir -p build
DSLITE_LOG="$PWD/build/dslite_flash.log"      # DSLite's own log (-g)
OUT_LOG="$PWD/build/flash_output.log"         # DSLite's console output
# The image needs an explicit file order (Meta Image 1 = order 1): FlashPython/mmWaveProgFlash rejects order 0
# ("File Order number value 0 is not in valid range (1-4)"), seen at the bench, firmware-10 Amendment 3. The
# order is given as "<file>,<order>" (accepted on the bench). DSLite's -e is --verbose,
# NOT erase (it grew dslite_flash.log to 52 MB), so it is not passed: format-on-download comes from the
# DownloadFormat setting, default true. The success line is an info: line of the flash script; if a run without
# -e ever lacks it, this script reports FAILED (fail-safe): then re-add -e.
CMD=("$DSLITE" flash -c "$CCXML" -s "COMPort=${PORT}" -g "$DSLITE_LOG" -f "${IMAGE},1")

echo "image : ${IMAGE#/build_context/} ($(stat -c %s "$IMAGE") B)"
echo "sha256: $(sha256sum "$IMAGE" | cut -d' ' -f1)"
echo "command: ${CMD[*]}"
if [[ $DRY -eq 1 ]]; then echo "(dry run: nothing flashed, no port touched)"; [[ -x "$DSLITE" ]] || { echo "ERROR: DSLite not found at $DSLITE (rebuild the image)" >&2; exit 3; }; exit 0; fi
[[ -x "$DSLITE" ]] || { echo "ERROR: DSLite not found at $DSLITE (rebuild the image: docker compose build)" >&2; exit 3; }

set +e
"${CMD[@]}" 2>&1 | tee "$OUT_LOG"
RC=${PIPESTATUS[0]}
set -e

OK_LN="$(grep -n 'SUCCESS!! File type META_IMAGE1' "$OUT_LOG" | head -1 | cut -d: -f1 || true)"
CANT_LN="$(grep -n "Can't Run Target CPU" "$OUT_LOG" | tail -1 | cut -d: -f1 || true)"
if [[ -z "$OK_LN" ]]; then
    echo "FAILED: no 'SUCCESS!! File type META_IMAGE1' in the DSLite output (rc=$RC); see $OUT_LOG" >&2
    exit 1
fi
if [[ $RC -ne 0 && ( -z "$CANT_LN" || "$CANT_LN" -le "$OK_LN" ) ]]; then
    echo "FAILED: DSLite rc=$RC without a trailing \"Can't Run Target CPU\" after the success line; see $OUT_LOG" >&2
    exit 1
fi
echo "Flashed (DSLite rc=$RC). Now power off, set functional mode (SOP0 only), power on, send a cfg at 115200 baud."
exit 0

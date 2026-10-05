#!/bin/bash
# Build TI's unmodified SDK 3.6 xwr18xx and xwr68xx mmw demos out of tree. Runs INSIDE the
# firmware container; start it from firmware_dev/ with
#     ./fw build ti_stock_demos [18xx|68xx ...]     (default: both)
# Nothing is written under /opt/ti: the SDK is overlaid in build/sdk/ (symlinks to the SDK,
# except the two demo folders, which are real copies that make builds in).
# Contract (projects/README.md): source project.env, work from this folder, write only to ./build/.
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
source ./project.env

PROJECT="$(basename "$PWD")"
OUT_DIR="$PWD/build"
OVL="${OUT_DIR}/sdk"
SDK_ROOT="${MMWAVE_SDK_PATH:-/opt/ti/mmwave_sdk_03_06_02_00-LTS}"
FAMILIES=("$@"); [[ ${#FAMILIES[@]} -gt 0 ]] || FAMILIES=(18xx 68xx)

# make_overlay SRC DST KEEP...: DST becomes a real directory whose entries symlink to SRC's,
# except entries on a KEEP path (relative to SRC): those are recursed into the same way, and the
# last component of each KEEP path is left absent (the caller creates it as a real copy).
make_overlay() {
    local src="$1" dst="$2"; shift 2
    local e n k rests
    mkdir -p "$dst"
    for e in "$src"/*; do
        n="$(basename "$e")"; rests=(); local hit=0
        for k in "$@"; do
            if [[ "${k%%/*}" == "$n" ]]; then
                hit=1; [[ "$k" == */* ]] && rests+=("${k#*/}")
            fi
        done
        if [[ $hit -eq 0 ]]; then ln -s "$e" "$dst/$n"
        elif [[ ${#rests[@]} -gt 0 ]]; then make_overlay "$e" "$dst/$n" "${rests[@]}"
        fi
    done
}

rm -rf "${OUT_DIR}"          # symlinks are removed, never followed
mkdir -p "${OVL}"
ln -s "${SDK_ROOT}/firmware" "${OVL}/firmware"      # radarss images: $(MMWAVE_SDK_INSTALL_PATH)/../firmware
KEEP=(); for fam in "${FAMILIES[@]}"; do KEEP+=("ti/demo/xwr${fam}/mmw"); done
make_overlay "${SDK_ROOT}/packages" "${OVL}/packages" "${KEEP[@]}"

for fam in "${FAMILIES[@]}"; do
    case "$fam" in
        18xx) DEVICE=iwr18xx; BOARD_NAME=iwr1843; TARGET=mmwDemo ;;
        68xx) DEVICE=iwr68xx; BOARD_NAME=iwr6843; TARGET=all ;;
        *) echo "ERROR: unknown family '$fam' (use 18xx or 68xx)" >&2; exit 2 ;;
    esac
    echo "=== Building xwr${fam} mmw demo (${DEVICE}) ==="
    DEMO_REL="ti/demo/xwr${fam}/mmw"
    DEMO_DIR="${OVL}/packages/${DEMO_REL}"
    mkdir -p "${DEMO_DIR}"
    # Copy the demo sources only: skip TI's prebuilt outputs (make would think they are current)
    # and the docs.
    tar -C "${SDK_ROOT}/packages/${DEMO_REL}" -cf - \
        --exclude=./docs --exclude='*.bin' --exclude='*.map' --exclude='*.xer4f' \
        --exclude='*.xe674' --exclude='*.rov.xs' . | tar -C "${DEMO_DIR}" -xf -

    # TI's setenv.sh must be sourced from its own folder; it sets the SDK/tool paths (/opt/ti)
    # and defaults MMWAVE_SDK_DEVICE=iwr68xx, so point it at the overlay afterwards.
    pushd "${SDK_ROOT}/packages/scripts/unix" > /dev/null
    set +u; source ./setenv.sh; set -u
    popd > /dev/null
    export MMWAVE_SDK_DEVICE="${DEVICE}"
    export MMWAVE_SDK_INSTALL_PATH="${OVL}/packages"
    export XWR18XX_RADARSS_IMAGE_BIN="${MMWAVE_SDK_INSTALL_PATH}/../firmware/radarss/xwr18xx_radarss_rprc.bin"
    export XWR68XX_RADARSS_IMAGE_BIN="${MMWAVE_SDK_INSTALL_PATH}/../firmware/radarss/xwr6xxx_radarss_rprc.bin"
    "${R4F_CODEGEN_INSTALL_PATH}/bin/armcl" --compiler_revision | head -1 | tee -a "${OUT_DIR}/compilers.txt"
    "${C674_CODEGEN_INSTALL_PATH}/bin/cl6x" --compiler_revision | head -1 | tee -a "${OUT_DIR}/compilers.txt"

    # 18xx: target mmwDemo = the standard image (`all` there would also build the AOP variants,
    # which the old reference script discarded). 68xx has no mmwDemo target; its `all` is the
    # standard image only.
    make -C "${DEMO_DIR}" "${TARGET}"

    cp "${DEMO_DIR}/xwr${fam}_mmw_demo.bin"          "${OUT_DIR}/${BOARD_NAME}_demo.bin"
    cp "${DEMO_DIR}/xwr${fam}_mmw_demo_mss.xer4f"    "${OUT_DIR}/${BOARD_NAME}_demo.elf"
    cp "${DEMO_DIR}/xwr${fam}_mmw_demo_mss.map"      "${OUT_DIR}/${BOARD_NAME}_demo_mss.map"
    cp "${DEMO_DIR}/xwr${fam}_mmw_demo_dss.map"      "${OUT_DIR}/${BOARD_NAME}_demo_dss.map"
done

# Provenance: what was built, from which commit, with which compilers.
cat > "${OUT_DIR}/build_info.txt" <<INFO
project=${PROJECT}
board=${BOARD}
sdk=${SDK} ${SDK_VERSION}
toolchain=${TOOLCHAIN}
compilers_run=$(tr '\n' ';' < "${OUT_DIR}/compilers.txt")
baseline=${BASELINE} @ ${BASELINE_COMMIT}
families=${FAMILIES[*]}
firmware_dev_commit=${FW_COMMIT:-unknown}
built_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
artifacts=${ARTIFACTS}
sha256:
$(cd "${OUT_DIR}" && sha256sum *.bin 2>/dev/null)
INFO
echo "Wrote ${OUT_DIR}/build_info.txt"

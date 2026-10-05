#!/bin/bash
# Build this project's xwr18xx mmw demo (src/) for the IWR1843 out of tree. Runs INSIDE the
# firmware container; start it from firmware_dev/ with
#     ./fw build iwr1843_sar_lvds
# Nothing is written under /opt/ti: the SDK is overlaid in build/sdk/ (symlinks to the SDK,
# except ti/demo/xwr18xx/mmw, which is a fresh copy of src/ that make builds in). Same technique
# as projects/ti_stock_demos/build.sh (copied, not sourced: projects are self-contained).
# Contract (projects/README.md): source project.env, work from this folder, write only to ./build/.
# Don't call git here: `fw` passes the firmware_dev commit in as FW_COMMIT.
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
source ./project.env

PROJECT="$(basename "$PWD")"
OUT_DIR="$PWD/build"
OVL="${OUT_DIR}/sdk"
SDK_ROOT="${MMWAVE_SDK_PATH:-/opt/ti/mmwave_sdk_03_06_02_00-LTS}"
DEMO_REL="ti/demo/xwr18xx/mmw"

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

# Fresh overlay every build, so the demo folder is always the current src/ (no stale copy, no
# stale objects). Symlinks are removed, never followed.
rm -rf "${OUT_DIR}"
mkdir -p "${OVL}"
ln -s "${SDK_ROOT}/firmware" "${OVL}/firmware"      # radarss images: $(MMWAVE_SDK_INSTALL_PATH)/../firmware
make_overlay "${SDK_ROOT}/packages" "${OVL}/packages" "${DEMO_REL}"
DEMO_DIR="${OVL}/packages/${DEMO_REL}"
mkdir -p "${DEMO_DIR}"
# Copy the project sources. Skip prebuilt outputs (make would think they are current) and docs,
# in case any are dropped into src/.
tar -C src -cf - \
    --exclude=./docs --exclude='*.bin' --exclude='*.map' --exclude='*.xer4f' \
    --exclude='*.xe674' --exclude='*.rov.xs' . | tar -C "${DEMO_DIR}" -xf -

# TI's setenv.sh must be sourced from its own folder; it sets the SDK/tool paths (/opt/ti)
# and defaults MMWAVE_SDK_DEVICE=iwr68xx, so point it at the device and the overlay afterwards.
pushd "${SDK_ROOT}/packages/scripts/unix" > /dev/null
set +u; source ./setenv.sh; set -u
popd > /dev/null
export MMWAVE_SDK_DEVICE=iwr18xx
export MMWAVE_SDK_INSTALL_PATH="${OVL}/packages"
export XWR18XX_RADARSS_IMAGE_BIN="${MMWAVE_SDK_INSTALL_PATH}/../firmware/radarss/xwr18xx_radarss_rprc.bin"
"${R4F_CODEGEN_INSTALL_PATH}/bin/armcl" --compiler_revision | head -1 | tee -a "${OUT_DIR}/compilers.txt"
"${C674_CODEGEN_INSTALL_PATH}/bin/cl6x" --compiler_revision | head -1 | tee -a "${OUT_DIR}/compilers.txt"

# mmwDemo = MSS-only metaimage (MSS + BSS firmware, DSS = NULL); see src/makefile.
make -C "${DEMO_DIR}" mmwDemo

cp "${DEMO_DIR}/xwr18xx_mmw_demo.bin"       "${OUT_DIR}/${PROJECT}.bin"
cp "${DEMO_DIR}/xwr18xx_mmw_demo_mss.xer4f" "${OUT_DIR}/${PROJECT}.elf"
cp "${DEMO_DIR}/xwr18xx_mmw_demo_mss.map"   "${OUT_DIR}/${PROJECT}_mss.map"

# Provenance: what was built, from which commit, with which compilers.
cat > "${OUT_DIR}/build_info.txt" <<INFO
project=${PROJECT}
board=${BOARD}
sdk=${SDK} ${SDK_VERSION}
toolchain=${TOOLCHAIN}
compilers_run=$(tr '\n' ';' < "${OUT_DIR}/compilers.txt")
baseline=${BASELINE} @ ${BASELINE_COMMIT}
make_target=mmwDemo
firmware_dev_commit=${FW_COMMIT:-unknown}
built_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
artifacts=${ARTIFACTS}
sha256:
$(cd "${OUT_DIR}" && sha256sum "${PROJECT}.bin")
INFO
echo "Wrote ${OUT_DIR}/build_info.txt"

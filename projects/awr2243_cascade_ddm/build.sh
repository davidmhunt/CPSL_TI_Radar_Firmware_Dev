#!/bin/bash
# Build the AM273x + AWR2243 2-chip cascade DDM demo. Runs INSIDE the firmware container; start it
# from firmware_dev/ with
#     ./fw build awr2243_cascade_ddm            (CCS_CONFIG=Debug for a debug build)
# Headless CCS build from the CCS projectspecs in src/ (mmwave2chipCascade_{dss,mss}.projectspec),
# matching how TI builds the Radar Toolbox lab. DSS is built first; the MSS post-build step
# combines both cores into the flashable .appimage.
# Contract (projects/README.md): work from this folder, write only to ./build/. Standard environment set
# by fw: FW_PROJECT, FW_VARIANT (unused here), FW_COMMIT. `fw build` writes build/build_info.json (sha256
# of every artifact) afterwards. Don't call git here: the submodule's .git is outside the container.
#
# TI_ROOT defaults to the container install dir; set TI_ROOT=~/ti to build natively.
# CCS_CONFIG selects the projectspec configuration (Release | Debug).
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

PROJECT="${FW_PROJECT:-$(basename "$PWD")}"
# Provenance strings come from project.toml (single-line `key = "value"` entries only).
toml_get() { sed -n "s/^$1 = \"\(.*\)\" *\(#.*\)\{0,1\}\$/\1/p" project.toml | head -n 1; }
SDK="$(toml_get sdk)"; SDK_VERSION="$(toml_get sdk_version)"; TOOLCHAIN="$(toml_get toolchain)"
BASELINE="$(toml_get baseline)"; BASELINE_COMMIT="$(toml_get baseline_commit)"
TI_ROOT="${TI_ROOT:-/opt/ti}"
CCS_CONFIG="${CCS_CONFIG:-Release}"

CCS_ECLIPSE="${TI_ROOT}/ccs/eclipse/eclipse"
SRC_DIR="$PWD/src"
OUT_DIR="$PWD/build"
STAGE_DIR="${OUT_DIR}/stage"          # patched copy of src/ that the projectspecs import from
WORKSPACE_DIR="${OUT_DIR}/ccs_workspace"
DSS_PROJECT=am273x_mmw_cascade_demo_dss
MSS_PROJECT=am273x_mmw_cascade_demo_mss

# The mmWave MCU+ SDK installer may nest its bundled components (MCU+ SDK AM273x, DFP,
# DSPLIB/MATHLIB) under its own top-level folder; CCS must discover products in both places.
SDK_TOP="${TI_ROOT}/mmwave_mcuplus_sdk_04_04_00_01"
PRODUCT_PATHS="${TI_ROOT};${SDK_TOP}"

# Prebuilt libraries the projectspecs copy from the source tree. They ship only in the Radar
# Toolbox; a copy committed in the repo wins (e.g. a library you've rebuilt), otherwise it's
# taken from the installed toolbox.
PREBUILT_LIBS=(
    ti/control/dpm/lib/libdpm_am273x.ae66
    ti/control/dpm/lib/libdpm_am273x.aer5f
    ti/control/mmwave/lib/libmmwave_cascade_am273x.aer5f
    ti/control/mmwavelink/lib/libmmwavelink_cascade_am273x.aer5f
    ti/datapath/dpu/dopplerprocDDMA/lib/libdopplerproc_hwa_ddma_cascade_am273x.ae66
    ti/datapath/dpu/rangeprocDDMA/lib/librangeproc_hwa_ddma_cascade_am273x.ae66
    ti/utils/cli/lib/libcli_cascade_am273x.aer5f
)

fail() { echo "ERROR: $*" >&2; exit 1; }

ccs() {
    "${CCS_ECLIPSE}" -noSplash -data "${WORKSPACE_DIR}" "$@"
}

[ -x "${CCS_ECLIPSE}" ] || fail "CCS not found at ${CCS_ECLIPSE}"
[ -d "${SDK_TOP}" ] || fail "mmWave MCU+ SDK not found at ${SDK_TOP}"

echo "=== Staging cascade source ==="
mkdir -p "${OUT_DIR}"
rm -rf "${STAGE_DIR}" "${WORKSPACE_DIR}"
mkdir -p "${STAGE_DIR}" "${WORKSPACE_DIR}"
cp -r "${SRC_DIR}/." "${STAGE_DIR}/"

RADAR_TOOLBOX_INSTALL_PATH="${RADAR_TOOLBOX_INSTALL_PATH:-${TI_ROOT}/radar_toolbox_4_00_00_05}"
RTB_CASCADE_DIR="${RADAR_TOOLBOX_INSTALL_PATH}/source/ti/examples/Automotive_ADAS_and_Parking/mmwave_2_chip_cascade/src/awr2243"
for lib in "${PREBUILT_LIBS[@]}"; do
    if [ -f "${STAGE_DIR}/${lib}" ]; then
        echo "  ${lib} (repo)"
    elif [ -f "${RTB_CASCADE_DIR}/${lib}" ]; then
        mkdir -p "$(dirname "${STAGE_DIR}/${lib}")"
        cp "${RTB_CASCADE_DIR}/${lib}" "${STAGE_DIR}/${lib}"
        echo "  ${lib} (radar toolbox)"
    else
        fail "missing prebuilt library ${lib} (not in repo or Radar Toolbox)"
    fi
done

# The projectspecs were authored on Windows: pre/post-build steps call CCS's bundled cygwin rm
# and SysConfig's node.exe. Point them at the Linux equivalents.
for spec in "${STAGE_DIR}"/*.projectspec; do
    sed -i -e 's|${CCE_INSTALL_ROOT}/utils/cygwin/rm|rm|g' \
           -e 's|nodejs/node\.exe|nodejs/node|g' "${spec}"
done

echo "=== Registering TI products and compilers with CCS ==="
ccs -application com.ti.common.core.initialize -ccs.productDiscoveryPath "${PRODUCT_PATHS}"
ccs -application com.ti.common.core.initialize -rtsc.productDiscoveryPath "${PRODUCT_PATHS}"

echo "=== Importing projectspecs ==="
ccs -application com.ti.ccstudio.apps.projectImport \
    -ccs.location "${STAGE_DIR}/mmwave2chipCascade_dss.projectspec" \
    -ccs.location "${STAGE_DIR}/mmwave2chipCascade_mss.projectspec" \
    -ccs.overwrite full

# projectBuild doesn't reliably return non-zero on compile errors, so check for the outputs too
echo "=== Building ${DSS_PROJECT} (${CCS_CONFIG}) ==="
ccs -application com.ti.ccstudio.apps.projectBuild -ccs.projects "${DSS_PROJECT}" \
    -ccs.configuration "${CCS_CONFIG}" -ccs.buildType full
DSS_OUT="${WORKSPACE_DIR}/${DSS_PROJECT}/${CCS_CONFIG}/${DSS_PROJECT}.xe66"
[ -f "${DSS_OUT}" ] || fail "DSS build produced no ${DSS_OUT}"

echo "=== Building ${MSS_PROJECT} (${CCS_CONFIG}) ==="
ccs -application com.ti.ccstudio.apps.projectBuild -ccs.projects "${MSS_PROJECT}" \
    -ccs.configuration "${CCS_CONFIG}" -ccs.buildType full
MSS_OUT="${WORKSPACE_DIR}/${MSS_PROJECT}/${CCS_CONFIG}/${MSS_PROJECT}.xer5f"
APPIMAGE="${WORKSPACE_DIR}/${MSS_PROJECT}/am273x_mmw_cascade_demo_DDM.appimage"
[ -f "${MSS_OUT}" ] || fail "MSS build produced no ${MSS_OUT}"
[ -f "${APPIMAGE}" ] || fail "MSS post-build produced no ${APPIMAGE}"

echo "=== Copying Cascade DDM build outputs ==="
cp "${APPIMAGE}" "${OUT_DIR}/am273x_cascade.appimage"
cp "${MSS_OUT}" "${OUT_DIR}/am273x_cascade.elf"
cp "${DSS_OUT}" "${OUT_DIR}/am273x_cascade_dss.xe66"

# Provenance: what was built, from which commit, with which config.
cat > "${OUT_DIR}/build_info.txt" <<INFO
project=${PROJECT}
board=AWR2243-2X-CAS-EVM
sdk=${SDK} ${SDK_VERSION}
toolchain=${TOOLCHAIN}
baseline=${BASELINE} @ ${BASELINE_COMMIT}
firmware_dev_commit=${FW_COMMIT:-unknown}
ccs_config=${CCS_CONFIG}
built_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
artifacts=am273x_cascade.appimage am273x_cascade.elf am273x_cascade_dss.xe66
INFO
ls -l "${OUT_DIR}"/am273x_cascade* "${OUT_DIR}/build_info.txt"
echo "=== Cascade DDM Build Succeeded ==="

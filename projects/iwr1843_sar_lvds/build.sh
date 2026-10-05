#!/bin/bash
# Build this project. Runs INSIDE the firmware container; start it from firmware_dev/ with
#     ./fw build <project> [args...]
# Contract (projects/README.md): source project.env, work from this folder, write only to ./build/.
# Don't call git here: the submodule's .git is outside the container. `fw` passes the
# firmware_dev commit in as FW_COMMIT.
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
source ./project.env

PROJECT="$(basename "$PWD")"
OUT_DIR="$PWD/build"
mkdir -p "${OUT_DIR}"

# ---- TODO: replace this stub with the real build -------------------------------------------
# Typical shape: copy src/ into build/ (TI makefiles and CCS write next to their sources), run
# the SDK make / CCS headless build there, then copy each file named in ARTIFACTS to build/.
echo "build.sh for '${PROJECT}' is still the template stub: no firmware was built."
# ---------------------------------------------------------------------------------------------

# Provenance: what was built, from which commit, with which config.
cat > "${OUT_DIR}/build_info.txt" <<EOF
project=${PROJECT}
board=${BOARD}
sdk=${SDK} ${SDK_VERSION}
toolchain=${TOOLCHAIN}
baseline=${BASELINE} @ ${BASELINE_COMMIT}
firmware_dev_commit=${FW_COMMIT:-unknown}
ccs_config=${CCS_CONFIG:-n/a}
built_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
artifacts=${ARTIFACTS}
EOF
echo "Wrote ${OUT_DIR}/build_info.txt"

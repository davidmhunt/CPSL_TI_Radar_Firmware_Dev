#!/bin/bash
# Build this project. Runs INSIDE the firmware container; start it from firmware_dev/ with
#     ./fw build <project> [--variant V]
# Standard environment (set by fw): FW_PROJECT, FW_VARIANT (empty if none), FW_COMMIT (firmware_dev
# short hash, `-dirty` if the tree is). Work from this folder and write only to ./build/. `fw build`
# writes build/build_info.json (sha256 of every artifact) afterwards. Don't call git here: the
# submodule's .git is outside the container.
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

PROJECT="${FW_PROJECT:-$(basename "$PWD")}"
VARIANT="${FW_VARIANT:-${1:-}}"
OUT_DIR="$PWD/build"
mkdir -p "${OUT_DIR}"

# ---- TODO: replace this stub with the real build -------------------------------------------
# Typical shape: copy src/ into build/ (TI makefiles and CCS write next to their sources), run
# the SDK make / CCS headless build there, then leave each [[artifact]] file of project.toml in build/.
echo "build.sh for '${PROJECT}' is still the template stub: no firmware was built."
# ---------------------------------------------------------------------------------------------

# Human-readable provenance (the machine-readable build_info.json is written by fw).
cat > "${OUT_DIR}/build_info.txt" <<INFO
project=${PROJECT}
variant=${VARIANT:-none}
firmware_dev_commit=${FW_COMMIT:-unknown}
ccs_config=${CCS_CONFIG:-n/a}
built_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
INFO
echo "Wrote ${OUT_DIR}/build_info.txt"

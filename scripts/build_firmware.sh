#!/usr/bin/env bash
#
# Build the Cat Food Machine firmware and stage release artifacts in dist/.
#
# This script is used by both the local container build (`make package`) and
# GitHub Actions, so keep it free of host-specific assumptions. It must run in
# an environment where ESP-IDF v5.5.5 has already been exported (the official
# espressif/idf:v5.5.5 container provides this).
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SRC_DIR="${REPO_ROOT}/src"
DIST_DIR="${REPO_ROOT}/dist"
BUILD_DIR="${SRC_DIR}/build"

log() {
    printf '[build] %s\n' "$*"
}

warn() {
    if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
        printf '::warning title=Firmware version mismatch::%s\n' "$*"
    else
        printf '[build] WARNING: %s\n' "$*" >&2
    fi
}

die() {
    printf '[build] ERROR: %s\n' "$*" >&2
    exit 1
}

if ! command -v idf.py >/dev/null 2>&1; then
    die "idf.py not found. Run this script inside the ESP-IDF environment."
fi

# ---------------------------------------------------------------------------
# Version consistency check (warnings only)
# ---------------------------------------------------------------------------
VERSION_FILE="${SRC_DIR}/version.txt"
VERSION="$(tr -d '[:space:]' < "${VERSION_FILE}")"
[[ -n "${VERSION}" ]] || die "src/version.txt is empty"

FIRMWARE_VERSION="$(sed -n 's/^[[:space:]]*#define[[:space:]]\{1,\}FIRMWARE_VERSION[[:space:]]\{1,\}"\([^"]*\)".*/\1/p' \
    "${SRC_DIR}/main/ui/src/setting_page.c" | head -n 1)"
CHANGELOG_VERSION="$(grep -m 1 -oE '^## \[[0-9]+\.[0-9]+\.[0-9]+[^]]*\]' "${REPO_ROOT}/CHANGELOG.md" \
    | sed -E 's/^## \[([^]]+)\]$/\1/' || true)"

log "Firmware version: ${VERSION}"

if [[ -n "${FIRMWARE_VERSION}" && "${FIRMWARE_VERSION}" != "${VERSION}" ]]; then
    warn "src/version.txt is ${VERSION}, but setting_page.c FIRMWARE_VERSION is ${FIRMWARE_VERSION}"
elif [[ -z "${FIRMWARE_VERSION}" ]]; then
    warn "Could not read FIRMWARE_VERSION from src/main/ui/src/setting_page.c"
fi

if [[ -n "${CHANGELOG_VERSION}" && "${CHANGELOG_VERSION}" != "${VERSION}" ]]; then
    warn "src/version.txt is ${VERSION}, but CHANGELOG.md latest version is ${CHANGELOG_VERSION}"
elif [[ -z "${CHANGELOG_VERSION}" ]]; then
    warn "Could not read the latest version from CHANGELOG.md"
fi

if [[ -n "${GITHUB_REF_NAME:-}" && "${GITHUB_REF_NAME}" == v* ]]; then
    TAG_VERSION="${GITHUB_REF_NAME#v}"
    if [[ "${TAG_VERSION}" != "${VERSION}" ]]; then
        warn "Git tag ${GITHUB_REF_NAME} does not match src/version.txt (${VERSION})"
    fi
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
APP_NAME="$(sed -n 's/^[[:space:]]*project(\([^ )]*\).*/\1/p' "${SRC_DIR}/CMakeLists.txt" | head -n 1)"
[[ -n "${APP_NAME}" ]] || die "Could not determine project name from src/CMakeLists.txt"

MERGED_NAME="${APP_NAME}-full.bin"

log "Building ${APP_NAME} with $(idf.py --version 2>/dev/null | head -n 1 || echo 'ESP-IDF')"
(
    cd "${SRC_DIR}"
    idf.py build
)

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

log "Creating merged flash image: ${MERGED_NAME}"
(
    cd "${SRC_DIR}"
    idf.py merge-bin -o "${DIST_DIR}/${MERGED_NAME}"
)

log "Generating size report"
(
    cd "${SRC_DIR}"
    idf.py size > "${DIST_DIR}/size.txt" 2>&1 || true
)

# ---------------------------------------------------------------------------
# Stage artifacts
# ---------------------------------------------------------------------------
cp "${BUILD_DIR}/bootloader/bootloader.bin" "${DIST_DIR}/bootloader.bin"
cp "${BUILD_DIR}/partition_table/partition-table.bin" "${DIST_DIR}/partition-table.bin"
cp "${BUILD_DIR}/${APP_NAME}.bin" "${DIST_DIR}/${APP_NAME}.bin"
cp "${BUILD_DIR}/flasher_args.json" "${DIST_DIR}/flasher_args.json"
cp "${BUILD_DIR}/${APP_NAME}.elf" "${DIST_DIR}/${APP_NAME}.elf"
cp "${BUILD_DIR}/${APP_NAME}.map" "${DIST_DIR}/${APP_NAME}.map"

(
    cd "${DIST_DIR}"
    sha256sum \
        bootloader.bin \
        partition-table.bin \
        "${APP_NAME}.bin" \
        "${MERGED_NAME}" \
        > SHA256SUMS.txt
)

log "Artifacts staged in ${DIST_DIR}:"
ls -lh "${DIST_DIR}"

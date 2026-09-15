#!/usr/bin/env bash
#
# scripts/check.sh - run linting and host unit tests only.
#
# This script NEVER flashes, erases or monitors a device.
#
# Stages:
#   1. clang-format (GNU) check
#   2. lizard cyclomatic-complexity check (CCN <= 10)
#   3. host unit tests for every component (Linux target)
#   4. per-file line coverage threshold (>= 50%)
#
# Usage: scripts/check.sh [--lint] [--test] [--coverage] [--all]
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPONENTS_DIR="${ROOT}/components"
LIBBSD_DIR="${ROOT}/.tools/libbsd"
CCN_LIMIT=10
COVERAGE_MIN=50

RUN_LINT=0
RUN_TEST=0
RUN_COVERAGE=0

STATUS=0
TMP_DIR="$(mktemp -d)"

log() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
ok() { printf '\033[32mPASS\033[0m %s\n' "$*"; }
ko() {
    printf '\033[31mFAIL\033[0m %s\n' "$*"
    STATUS=1
}

usage() {
    echo "usage: $0 [--lint] [--test] [--coverage] [--all]"
    exit 0
}

parse_args() {
    if [ "$#" -eq 0 ]; then
        RUN_LINT=1
        RUN_TEST=1
        RUN_COVERAGE=1
    fi
    for arg in "$@"; do
        case "$arg" in
        --lint) RUN_LINT=1 ;;
        --test) RUN_TEST=1 ;;
        --coverage) RUN_COVERAGE=1 ;;
        --all)
            RUN_LINT=1
            RUN_TEST=1
            RUN_COVERAGE=1
            ;;
        -h | --help) usage ;;
        *)
            echo "unknown option: $arg"
            usage
            ;;
        esac
    done
}

sources() {
    find "${ROOT}/components" "${ROOT}/main" -type f \( -name '*.c' -o -name '*.h' \) \
        -not -path '*/build/*' -not -path '*/managed_components/*'
}

# The ESP-IDF Linux target needs the libbsd headers. If they are missing from
# the system, fetch the package locally (no root required) and point the
# compiler at the extracted copy.
setup_libbsd() {
    if [ -f /usr/include/x86_64-linux-gnu/bsd/sys/cdefs.h ] ||
        [ -f /usr/include/bsd/sys/cdefs.h ]; then
        return
    fi
    if [ ! -f "${LIBBSD_DIR}/usr/include/x86_64-linux-gnu/bsd/sys/cdefs.h" ]; then
        echo "Fetching libbsd-dev headers into ${LIBBSD_DIR} ..."
        mkdir -p "${LIBBSD_DIR}"
        (
            cd "${LIBBSD_DIR}" &&
                apt-get download libbsd-dev >/dev/null 2>&1 &&
                dpkg-deb -x libbsd-dev_*.deb .
        ) || {
            ko "could not fetch libbsd-dev headers"
            return
        }
    fi
    export CPATH="${LIBBSD_DIR}/usr/include/x86_64-linux-gnu${CPATH:+:${CPATH}}"
    export LIBRARY_PATH="${LIBBSD_DIR}/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:${LIBRARY_PATH}}"
}

run_lint() {
    log "clang-format (GNU)"
    if clang-format --dry-run --Werror $(sources) 2>"${TMP_DIR}/format.err"; then
        ok "clang-format"
    else
        cat "${TMP_DIR}/format.err"
        ko "clang-format"
    fi

    log "lizard CCN <= ${CCN_LIMIT}"
    local out
    out="$(lizard -C "${CCN_LIMIT}" -w "${COMPONENTS_DIR}" "${ROOT}/main" 2>&1)"
    if [ -n "${out}" ]; then
        echo "${out}"
        ko "lizard"
    else
        ok "lizard"
    fi
}

component_coverage() {
    local json="$1"
    python3 - "${json}" "${COVERAGE_MIN}" <<'PY'
import json
import sys

data = json.load(open(sys.argv[1]))
minimum = float(sys.argv[2])
bad = []
for entry in data.get("files", []):
    percent = entry.get("line_percent")
    if percent is None:
        continue
    if percent < minimum:
        bad.append((entry.get("filename"), percent))
for name, percent in bad:
    print(f"  {percent:5.1f}%  {name}")
sys.exit(1 if bad else 0)
PY
}

run_component_tests() {
    local comp
    for comp in "${COMPONENTS_DIR}"/*/; do
        local app="${comp}test_apps/host"
        [ -d "${app}" ] || continue
        local name
        name="$(basename "${comp}")"
        log "host tests: ${name}"

        if [ ! -f "${app}/sdkconfig" ]; then
            if ! (cd "${app}" && idf.py --preview set-target linux >/dev/null 2>&1); then
                ko "set-target linux (${name})"
                continue
            fi
        fi
        if ! (cd "${app}" && idf.py build >"${TMP_DIR}/${name}.build" 2>&1); then
            tail -20 "${TMP_DIR}/${name}.build"
            ko "build (${name})"
            continue
        fi

        local elf
        elf="$(find "${app}/build" -maxdepth 1 -name '*.elf' | head -1)"
        if [ -z "${elf}" ]; then
            ko "no test binary (${name})"
            continue
        fi

        if "${elf}" >"${TMP_DIR}/${name}.test" 2>&1; then
            ok "tests (${name})"
        else
            grep -E "FAIL|Tests" "${TMP_DIR}/${name}.test" | tail -10
            ko "tests (${name})"
        fi

        if [ "${RUN_COVERAGE}" -eq 1 ]; then
            local json="${TMP_DIR}/${name}.coverage.json"
            gcovr --root "${ROOT}" --filter "${comp}" --json "${json}" "${app}/build" \
                >/dev/null 2>&1
            if component_coverage "${json}"; then
                ok "coverage >= ${COVERAGE_MIN}% (${name})"
            else
                ko "coverage < ${COVERAGE_MIN}% (${name})"
            fi
        fi
    done
}

main() {
    parse_args "$@"
    setup_libbsd
    [ "${RUN_LINT}" -eq 1 ] && run_lint
    if [ "${RUN_TEST}" -eq 1 ]; then
        run_component_tests
    fi

    echo
    if [ "${STATUS}" -eq 0 ]; then
        printf '\033[32mALL CHECKS PASSED\033[0m\n'
    else
        printf '\033[31mCHECKS FAILED\033[0m\n'
    fi
    rm -rf "${TMP_DIR}"
    exit "${STATUS}"
}

main "$@"

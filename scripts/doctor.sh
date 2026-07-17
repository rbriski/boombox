#!/usr/bin/env bash
# Environment health check for Boombox development. Read-only and safe:
# checks the host, the pinned toolchain, and (informationally) the serial
# port. Never writes anything and never talks to the device.
#
# Exit code: 0 = ready to build (warnings possible), 1 = blocking problems.
set -uo pipefail

# shellcheck source=scripts/env.sh
source "$(cd "$(dirname "$0")" && pwd)/env.sh"

FAILURES=0
warn() { printf 'WARN  %s\n' "$1"; }
pass() { printf 'ok    %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; FAILURES=$((FAILURES + 1)); }

echo "Boombox doctor — pin: ESP-IDF $IDF_VERSION"
echo

# Host prerequisites
for tool in git cmake; do
    if command -v "$tool" >/dev/null 2>&1; then
        pass "$tool: $(command -v "$tool")"
    else
        fail "$tool not found — on macOS: brew install cmake python"
    fi
done

if python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
    pass "python3 >= 3.10 ($(python3 --version 2>&1))"
else
    fail "python3 >= 3.10 required for ESP-IDF v6 (found: $(python3 --version 2>&1)) — brew install python"
fi

# Pinned ESP-IDF install
if [ -d "$IDF_INSTALL_DIR" ]; then
    pass "ESP-IDF present: $IDF_INSTALL_DIR"
    tag="$(git -C "$IDF_INSTALL_DIR" describe --tags --exact-match 2>/dev/null || true)"
    if [ -z "$tag" ]; then
        warn "cannot verify IDF tag (not a git checkout?) — ensure it is $IDF_VERSION"
    elif [ "$tag" = "$IDF_VERSION" ]; then
        pass "ESP-IDF tag matches pin ($tag)"
    else
        fail "ESP-IDF at $IDF_INSTALL_DIR is '$tag', pin is '$IDF_VERSION' — rerun scripts/bootstrap.sh"
    fi
    if [ -f "$IDF_INSTALL_DIR/export.sh" ]; then
        pass "export.sh present"
    else
        fail "export.sh missing under $IDF_INSTALL_DIR — install is broken; rerun scripts/bootstrap.sh"
    fi
    if [ -d "${IDF_TOOLS_PATH:-$HOME/.espressif}" ]; then
        pass "IDF tools dir present (${IDF_TOOLS_PATH:-$HOME/.espressif})"
    else
        warn "IDF tools dir missing — run scripts/bootstrap.sh (install.sh step)"
    fi
else
    fail "ESP-IDF not installed at $IDF_INSTALL_DIR — run scripts/bootstrap.sh"
fi

# Serial port (informational: absence never blocks a build)
case "$(uname -s)" in
Darwin)
    ports="$(ls /dev/cu.usbserial* /dev/cu.wchusbserial* 2>/dev/null || true)"
    ;;
*)
    ports="$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)"
    ;;
esac
if [ -n "$ports" ]; then
    pass "serial port(s) visible: $(echo "$ports" | tr '\n' ' ')"
else
    warn "no USB serial port visible — device unplugged, or driver needed (docs/setup-research.html §4)"
fi

echo
if [ "$FAILURES" -gt 0 ]; then
    echo "doctor: $FAILURES blocking problem(s) — fix the FAIL lines above."
    exit 1
fi
echo "doctor: environment ready."

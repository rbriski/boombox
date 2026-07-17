#!/usr/bin/env bash
# Run idf.py inside the pinned ESP-IDF environment. Default action: build.
#
#   scripts/build.sh                 # idf.py build
#   scripts/build.sh size            # any idf.py args pass through
#   scripts/build.sh -p <port> flash monitor   # explicit, deliberate flashing —
#                                    # read docs/hardware.md safety rules first
set -euo pipefail

# shellcheck source=scripts/env.sh
source "$(cd "$(dirname "$0")" && pwd)/env.sh"

if [ ! -f "$IDF_INSTALL_DIR/export.sh" ]; then
    echo "error: pinned ESP-IDF ($IDF_VERSION) not found at $IDF_INSTALL_DIR." >&2
    echo "Run scripts/bootstrap.sh first (or set BOOMBOX_IDF_PATH)." >&2
    exit 1
fi

# export.sh is not `set -u`-clean; relax while sourcing it.
set +u
# shellcheck disable=SC1091
source "$IDF_INSTALL_DIR/export.sh" >/dev/null
set -u

cd "$BOOMBOX_ROOT"
exec idf.py "${@:-build}"

# Shared environment resolution for Boombox scripts. Source this; don't run it.
#
# Resolves the pinned ESP-IDF version (.espidf-version at the repo root) and
# where that IDF lives. The default install dir is version-suffixed so multiple
# pins can coexist across projects; set BOOMBOX_IDF_PATH to reuse an existing
# install (e.g. one managed by EIM) — doctor.sh will verify its tag.

BOOMBOX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_VERSION="$(tr -d '[:space:]' < "$BOOMBOX_ROOT/.espidf-version")"
IDF_INSTALL_DIR="${BOOMBOX_IDF_PATH:-$HOME/esp/esp-idf-$IDF_VERSION}"

if [ -z "$IDF_VERSION" ]; then
    echo "error: $BOOMBOX_ROOT/.espidf-version is empty" >&2
    exit 1
fi

#!/system/bin/sh
# No APK repacking, external overlay, policy changes, or automatic game launch.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PACKAGE=com.catsbit.oxidesurvivalisland
EXPECTED_VERSION=1.14.11949
MODE=${1:---load}
case "$MODE" in --load|--check) ;; *) echo "Usage: sh run-root.sh [--check|--load]" >&2; exit 2;; esac
if [ "$(id -u)" != 0 ]; then
    echo 'Root required: su -c "sh /data/local/tmp/EncoreDLC3_Internal/run-root.sh"' >&2; exit 1
fi
if [ "$(uname -m)" != aarch64 ]; then
    echo 'This build requires an ARM64 Android device.' >&2; exit 2
fi
# Override only the file location, not the target package or verified dump version.
LIB=${ENCORE_LIBRARY_PATH:-$HERE/libEncore.so}
LOADER=$HERE/encore-loader
if [ ! -r "$LIB" ] || [ ! -x "$LOADER" ]; then
    echo "Missing freshly built libEncore.so / executable encore-loader in $HERE" >&2; exit 3
fi
VERSION=$(dumpsys package "$PACKAGE" | sed -n 's/^[[:space:]]*versionName=//p' | head -n 1 | tr -d '\r')
if [ "$VERSION" != "$EXPECTED_VERSION" ]; then
    echo "Refusing mismatched/unknown game version: '${VERSION:-unknown}', expected $EXPECTED_VERSION." >&2
    echo 'Do not reuse these RVAs for a different game build. Check the dump and update the project first.' >&2
    exit 4
fi
PIDS=$(pidof "$PACKAGE" || true)
set -- $PIDS
if [ "$#" != 1 ]; then
    echo "Open the game first; expected one main process, found: ${PIDS:-none}" >&2; exit 5
fi
PID=$1
case "$PID" in ''|*[!0-9]*) echo 'Invalid PID' >&2; exit 5;; esac
printf 'Target: %s / %s / pid %s\nLibrary: %s\n' "$PACKAGE" "$VERSION" "$PID" "$LIB"
if [ "$MODE" = --check ]; then
    echo 'Launcher preflight passed. ptrace/dlopen/hooks have NOT been tested.'
    exit 0
fi
exec "$LOADER" "$PID" "$LIB"

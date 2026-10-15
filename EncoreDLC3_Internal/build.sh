#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NDK="${NDK_PATH:-${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}}"
if [[ -n "$NDK" ]]; then
    TOOL="$NDK/ndk-build"
else
    TOOL="$(command -v ndk-build || true)"
fi
if [[ -z "$TOOL" || ! -x "$TOOL" ]]; then
    echo 'Android NDK missing. Use NDK_PATH=/path/to/android-ndk ./build.sh' >&2
    exit 1
fi
python3 "$ROOT/tools/offsets.py"
python3 "$ROOT/tools/source_audit.py"
"$TOOL" NDK_PROJECT_PATH="$ROOT" \
    APP_BUILD_SCRIPT="$ROOT/jni/Android.mk" NDK_APPLICATION_MK="$ROOT/jni/Application.mk" \
    NDK_OUT="$ROOT/build/obj" NDK_LIBS_OUT="$ROOT/build/libs" -j"${JOBS:-2}" "$@"
for artifact in libEncore.so encore-loader; do
    test -s "$ROOT/build/libs/arm64-v8a/$artifact" || { echo "Missing $artifact" >&2; exit 1; }
done
echo "Built: $ROOT/build/libs/arm64-v8a/{libEncore.so,encore-loader}"

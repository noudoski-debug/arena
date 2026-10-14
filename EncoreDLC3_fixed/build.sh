#!/usr/bin/env bash
# build.sh — сборка оверлея OxideBase (ndk-build).
#
#   ./build.sh                      # OpenGL ES3 (по умолчанию)
#   OPENGL_DRAW=0 ./build.sh        # Vulkan-ветка
#   MODULE_NAME=oxide ./build.sh    # другое имя бинарника
#   NDK_PATH=/path/to/android-ndk ./build.sh
#   ./build.sh V=1 ndk-build-аргументы  (всё, что не распознано, уходит в ndk-build)
#
# Структура проекта: jni/Android.mk + jni/Application.mk + jni/src + jni/include,
# поэтому из корня достаточно просто `ndk-build` — этот скрипт только находит NDK
# и складывает артефакты в build/ (а не в jni/obj, jni/libs).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

find_ndk() {
    local cand
    for cand in "${NDK_PATH:-}" "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK:-}" \
                "${ANDROID_NDK_ROOT:-}" /opt/android-ndk /usr/lib/android-ndk; do
        [[ -n "$cand" && -x "$cand/ndk-build" ]] && { printf '%s\n' "$cand"; return 0; }
    done
    # AIDE / AIDE-Studio держат NDK в no_backup, каталог с версией в имени
    for cand in /data/user/0/com.goxome.aidestudio/no_backup/ndksupport-*/android-ndk-aide \
                /data/data/com.goxome.aidestudio/no_backup/ndksupport-*/android-ndk-aide \
                "$HOME"/android-ndk* /opt/android-ndk* /usr/local/android-ndk*; do
        [[ -x "$cand/ndk-build" ]] && { printf '%s\n' "$cand"; return 0; }
    done
    return 1
}

if ! NDK="$(find_ndk)"; then
    cat >&2 <<'MSG'
build.sh: ndk-build не найден.
  Укажи путь вручную:  NDK_PATH=/path/to/android-ndk ./build.sh
  (в AIDE ничего указывать не нужно — проект собирается сам, т.к. jni/Android.mk на месте)
MSG
    exit 1
fi

JOBS="${JOBS:-$( (nproc 2>/dev/null || getconf _NPROCESSORS_ONLN) )}"
MODULE="${MODULE_NAME:-cheat}"

extra=()
[[ -n "${OPENGL_DRAW:-}" ]] && extra+=("OPENGL_DRAW=$OPENGL_DRAW")
[[ -n "${MODULE_NAME:-}" ]] && extra+=("MODULE_NAME=$MODULE_NAME")
[[ -n "${WARNING_FLAGS:-}" ]] && extra+=("WARNING_FLAGS=$WARNING_FLAGS")

"$NDK/ndk-build" \
    NDK_PROJECT_PATH="$ROOT" \
    APP_BUILD_SCRIPT="$ROOT/jni/Android.mk" \
    NDK_APPLICATION_MK="$ROOT/jni/Application.mk" \
    NDK_OUT="$ROOT/build/obj" \
    NDK_LIBS_OUT="$ROOT/build/libs" \
    ${extra[@]+"${extra[@]}"} \
    -j"$JOBS" \
    "$@"

out="$ROOT/build/libs/arm64-v8a/$MODULE"
if [[ -f "$out" ]]; then
    printf '\n[build.sh] OK -> %s (%s)\n' "$out" "$(du -h "$out" | cut -f1)"
else
    printf '\n[build.sh] сборка завершилась, но %s не найден\n' "$out" >&2
    exit 1
fi

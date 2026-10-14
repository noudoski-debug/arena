#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/host-tests"
mkdir -p "$OUT"
python3 -m unittest discover -s "$ROOT/tests" -p 'test_*.py' -v
python3 "$ROOT/tools/offsets.py"
bash -n "$ROOT/build.sh"
sh -n "$ROOT/run-root.sh"
CXX=${CXX:-g++}
INC=(-I"$ROOT/jni/include" -I"$ROOT/jni/include/ImGui" -I"$ROOT/jni/include/Hook/Dobby")
"$CXX" -std=c++17 "$ROOT/tests/test_maps.cpp" -o "$OUT/maps"
# Compile the Android profile implementation with a deliberately unavailable
# filesystem header, matching the older AIDE NDK reported by users.
mkdir -p "$OUT/no-filesystem"
printf '#error Android profile code must not depend on filesystem\n' > "$OUT/no-filesystem/filesystem"
"$CXX" -std=c++17 -DDEAR_HOST_TEST "${INC[@]}" -I"$OUT/no-filesystem" -c "$ROOT/jni/src/Config.cpp" -o "$OUT/config.o"
"$CXX" -std=c++17 -DDEAR_HOST_TEST "${INC[@]}" "$ROOT/tests/test_modules.cpp" "$OUT/config.o" "$ROOT/jni/src/Effects.cpp" -o "$OUT/modules"
"$OUT/modules"
"$CXX" -std=c++17 -DDEAR_HOST_TEST "${INC[@]}" "$ROOT/tests/test_config_fs.cpp" "$OUT/config.o" -o "$OUT/config-fs"
"$OUT/config-fs"
"$OUT/maps"
"$CXX" -std=c++17 -pthread "${INC[@]}" -I"$ROOT/tests/stubs" "$ROOT/tests/test_runtime.cpp" -ldl -o "$OUT/runtime"
"$OUT/runtime"
"$CXX" -std=c++17 -pthread "${INC[@]}" "$ROOT/tests/test_menu.cpp" "$ROOT/jni/src/Menu.cpp" "$ROOT/jni/src/Chams.cpp" "$ROOT/jni/src/Config.cpp" "$ROOT/jni/src/Effects.cpp" \
    "$ROOT/jni/src/ImGui/imgui.cpp" "$ROOT/jni/src/ImGui/imgui_draw.cpp" \
    "$ROOT/jni/src/ImGui/imgui_tables.cpp" "$ROOT/jni/src/ImGui/imgui_widgets.cpp" -o "$OUT/menu"
"$OUT/menu"
"$CXX" -std=c++17 -D__aarch64__ -fsyntax-only "${INC[@]}" "$ROOT/jni/loader/Loader.cpp"
echo 'PASS: host tests + loader syntax. Android compile/device injection remain untested.'

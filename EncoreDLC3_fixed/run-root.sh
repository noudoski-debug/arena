#!/system/bin/sh
set -eu
BIN="${1:-/data/local/tmp/EncoreDLC/cheat}"
if [ "$(id -u)" != "0" ]; then
    echo "Нужен root: запусти скрипт через su." >&2
    exit 1
fi
if [ ! -f "$BIN" ]; then
    echo "Бинарник не найден: $BIN" >&2
    exit 2
fi
chmod 755 "$BIN"
exec "$BIN"

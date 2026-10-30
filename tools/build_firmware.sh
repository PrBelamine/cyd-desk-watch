#!/usr/bin/env bash
# Builds flasher/desk-watch/firmware.bin (bootloader + partitions + app, flashed at offset 0).
# Needs PlatformIO: pip install platformio
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
mkdir -p "$WORK/src"
cp "$ROOT"/Desk_Watch_CYD_V1/*.h "$ROOT"/Desk_Watch_CYD_V1/*.cpp "$WORK/src/"
{ echo '#include <Arduino.h>'; cat "$ROOT/Desk_Watch_CYD_V1/Desk_Watch_CYD_V1.ino"; } > "$WORK/src/main.cpp"
rm -f "$WORK/src/Secrets.h"                  # never bake a Wi-Fi password into a published file
cp "$ROOT/tools/pio/platformio.ini" "$WORK/"
(cd "$WORK" && pio run)
mkdir -p "$ROOT/flasher/desk-watch"
cp "$WORK/.pio/build/cyd/firmware.factory.bin" "$ROOT/flasher/desk-watch/firmware.bin"
echo "OK: flasher/desk-watch/firmware.bin"

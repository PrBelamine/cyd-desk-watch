#!/usr/bin/env bash
# Builds flasher/desk-watch/<edition>/firmware.bin (bootloader + partitions + app, flashed at offset 0).
# The edition number is read from Desk_Watch_CYD_V1/Settings.h. Needs PlatformIO: pip install platformio
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EDITION="$(sed -n 's/.*FIRMWARE_EDITION\[\] *= *"\([^"]*\)".*/\1/p' "$ROOT/Desk_Watch_CYD_V1/Settings.h")"
[ -n "$EDITION" ] || { echo "FIRMWARE_EDITION not found in Settings.h"; exit 1; }
WORK="$(mktemp -d)"
mkdir -p "$WORK/src"
cp "$ROOT"/Desk_Watch_CYD_V1/*.h "$ROOT"/Desk_Watch_CYD_V1/*.cpp "$WORK/src/"
{ echo '#include <Arduino.h>'; cat "$ROOT/Desk_Watch_CYD_V1/Desk_Watch_CYD_V1.ino"; } > "$WORK/src/main.cpp"
rm -f "$WORK/src/Secrets.h"                  # never bake a Wi-Fi password into a published file
cp "$ROOT/tools/pio/platformio.ini" "$ROOT/tools/pio/patch_audio.py" "$WORK/"
(cd "$WORK" && pio run)
OUT="$ROOT/flasher/desk-watch/$EDITION"
mkdir -p "$OUT"
cp "$WORK/.pio/build/cyd/firmware.factory.bin" "$OUT/firmware.bin"
echo "OK: flasher/desk-watch/$EDITION/firmware.bin"

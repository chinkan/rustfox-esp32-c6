#!/bin/sh
# Configure and build the ESP32-C6 image. Requires ESP-IDF 5.4 or newer.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
IDF_PATH=${IDF_PATH:-"$HOME/esp/esp-idf"}
if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "ESP-IDF not found at $IDF_PATH" >&2
    echo "Clone a 5.5 release and run ./install.sh esp32c6" >&2
    exit 1
fi
# export.sh expects to be sourced.
. "$IDF_PATH/export.sh"
cd "$ROOT"
idf.py set-target esp32c6
idf.py build

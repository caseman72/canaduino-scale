#!/bin/bash
# OTA upload script for scale-monitor
#
# First flash (new nano, via USB):
#   esphome run scale-monitor.yaml  # pick the USB serial port
# After first flash, device shows up as scale-monitor.local:
#   ./upload.sh
# Or by IP:
#   ./upload.sh 172.24.1.x
#
# Splits compile + upload because PlatformIO's espressif32 platform blocks
# Python 3.14 (used by Homebrew esphome). The compile step tolerates it but
# the upload step re-runs the version check and aborts — so we compile with
# esphome, then upload the built firmware.ota.bin via esphome.espota2 directly.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEVICE="${1:-scale-monitor.local}"
CONFIG="${2:-scale-monitor.yaml}"
SECRETS="${3:-secrets.h}"

if [[ ! -f "$SCRIPT_DIR/$SECRETS" ]]; then
    echo "Error: ${SECRETS} not found. Copy secrets.example.h to ${SECRETS} and fill in values."
    exit 1
fi

parse_secret() {
    # Robust against trailing inline comments that may contain quote
    # characters (e.g. "5'10\""). Strips the "#define KEY "" prefix and
    # everything from the closing quote onward.
    grep "^#define $1 " "$SCRIPT_DIR/$SECRETS" \
        | sed 's/^#define [A-Z_]* "//' \
        | sed 's/".*$//'
}

WIFI_SSID=$(parse_secret WIFI_SSID)
WIFI_PASSWORD=$(parse_secret WIFI_PASSWORD)
MQTT_BROKER=$(parse_secret MQTT_BROKER)
MQTT_PORT=$(parse_secret MQTT_PORT)
MQTT_USERNAME=$(parse_secret MQTT_USERNAME)
MQTT_PASSWORD=$(parse_secret MQTT_PASSWORD)
OTA_PASSWORD=$(parse_secret OTA_PASSWORD)
SCALE_MAC=$(parse_secret SCALE_MAC)
USER_HEIGHT_M=$(parse_secret USER_HEIGHT_M)
USER_AGE=$(parse_secret USER_AGE)
USER_SEX=$(parse_secret USER_SEX)

CONFIG_NAME="${CONFIG%.yaml}"
FIRMWARE="$SCRIPT_DIR/.esphome/build/$CONFIG_NAME/.pioenvs/$CONFIG_NAME/firmware.ota.bin"

cd "$SCRIPT_DIR"

echo "Compiling $CONFIG..."
esphome \
    -s wifi_ssid "$WIFI_SSID" \
    -s wifi_password "$WIFI_PASSWORD" \
    -s mqtt_broker "$MQTT_BROKER" \
    -s mqtt_port "$MQTT_PORT" \
    -s mqtt_username "$MQTT_USERNAME" \
    -s mqtt_password "$MQTT_PASSWORD" \
    -s ota_password "$OTA_PASSWORD" \
    -s scale_mac "$SCALE_MAC" \
    -s user_height_m "$USER_HEIGHT_M" \
    -s user_age "$USER_AGE" \
    -s user_sex "$USER_SEX" \
    compile "$CONFIG"

if [[ ! -f "$FIRMWARE" ]]; then
    echo "Error: firmware not found at $FIRMWARE"
    exit 1
fi

echo "Uploading to $DEVICE via OTA..."
ESPHOME_PY="$(head -1 "$(command -v esphome)" | sed 's|^#!||')"
OTA_PASSWORD="$OTA_PASSWORD" DEVICE="$DEVICE" FIRMWARE="$FIRMWARE" \
"$ESPHOME_PY" - <<'PY'
import os, sys
from pathlib import Path
from esphome.espota2 import run_ota
rc, host = run_ota(os.environ["DEVICE"], 3232, os.environ["OTA_PASSWORD"], Path(os.environ["FIRMWARE"]))
if rc != 0:
    print(f"OTA failed (rc={rc})", file=sys.stderr)
    sys.exit(rc)
print(f"OTA complete — device at {host}")
PY

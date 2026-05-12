#!/bin/bash
# Subscribe to scale MQTT debug logs

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SECRETS_FILE="$SCRIPT_DIR/secrets.h"

if [[ ! -f "$SECRETS_FILE" ]]; then
    echo "Error: secrets.h not found at $SECRETS_FILE"
    exit 1
fi

parse_secret() {
    grep "^#define $1 " "$SECRETS_FILE" \
        | sed 's/^#define [A-Z_]* "//' \
        | sed 's/".*$//'
}

MQTT_BROKER=$(parse_secret MQTT_BROKER)
MQTT_USER=$(parse_secret MQTT_USERNAME)
MQTT_PASS=$(parse_secret MQTT_PASSWORD)
MQTT_PORT=$(parse_secret MQTT_PORT)

TOPIC="${1:-scale/#}"

mosquitto_sub -h "$MQTT_BROKER" -p "$MQTT_PORT" -u "$MQTT_USER" -P "$MQTT_PASS" -t "$TOPIC" -v \
    | sed -e 's/\x1B\[[0-9;]*[a-zA-Z]//g'

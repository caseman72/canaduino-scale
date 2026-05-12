// Scale monitor secrets — TEMPLATE
// Copy to secrets.h and fill in your own values. secrets.h is gitignored.

// WiFi
#define WIFI_SSID "your_ssid"
#define WIFI_PASSWORD "your_password"

// MQTT broker
#define MQTT_BROKER "your_broker_host_or_ip"
#define MQTT_PORT "1883"
#define MQTT_USERNAME "your_username"
#define MQTT_PASSWORD "your_password"

// OTA password
#define OTA_PASSWORD "your_ota_password"

// ESPHome native API encryption key (base64, 32 bytes).
// Generate with:
//   python3 -c "import secrets, base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
// Not used in this firmware (we only use MQTT) but defined for parity with
// other Canaduino projects that may want HA's native ESPHome integration.
#define API_ENCRYPTION_KEY "generate_a_base64_32byte_key"

// Eufy scale BLE MAC. Find by scanning nearby BLE with nRF Connect (or any
// BLE scanner app) while the scale is awake. Tested on T9130; the same
// adv-data format may work on other Eufy bathroom scales (T9140, T9148, etc.)
// but is not guaranteed.
#define SCALE_MAC "AA:BB:CC:DD:EE:FF"

// User profile for body-composition equations (Kyle / Janssen / Mifflin-St
// Jeor). The scale's adv frame doesn't carry user identity, so this firmware
// assumes a single user. Multi-user setups need per-user firmware builds or
// an HA-side mux.
//   USER_HEIGHT_M : height in meters (1.778 m = 5'10")
//   USER_AGE      : whole years
//   USER_SEX      : 1 = male, 0 = female
#define USER_HEIGHT_M "1.778"
#define USER_AGE "30"
#define USER_SEX "1"

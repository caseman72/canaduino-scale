# Scale Monitor (Eufy T9130 via BLE adv → MQTT / Home Assistant)

ESPHome firmware for an Arduino Nano ESP32 (on a Canaduino PLC baseboard)
that passively listens to a Eufy T9130 smart scale's BLE advertisements,
decodes weight + heart rate + impedance, derives a stack of body-composition
metrics from public BIA equations, and publishes everything to MQTT for HA
auto-discovery. No GATT connection, no Eufy app login, no Anker cloud.

A virtual "Weigh In" button arms a 90-second capture window. When the scale
broadcasts its final "saved" frame (status byte `& 0x80`), the firmware
publishes one snapshot of all sensors and auto-disarms. An onboard LED
indicates state: off → blink → solid (3 s) → off.

## What you get in HA

Auto-discovered as a "Scale Monitor" device with 18 entities:

| Direct measurement | From the scale's BLE adv |
|---|---|
| Weight (kg / lb) | weight bytes ÷ 100 |
| Heart Rate (BPM) | final-frame byte |
| Impedance (Ω) | impedance bytes ÷ 10 |

| Derived (BIA equations) | Formula |
|---|---|
| BMI | kg / m² |
| Fat-Free Mass (kg / lb) | Kyle 2001 (Geneva) |
| Body Fat (% / kg / lb) | (weight − FFM) / weight |
| Total Body Water (kg / lb / %) | FFM × 0.732 |
| Skeletal Muscle Mass (kg / lb) | Janssen 2000 |
| BMR (kcal/day) | Mifflin-St Jeor |
| Protein (%) | FFM × 0.21 |
| Visceral Fat Index | heuristic (BMI + BF% + age + SMM ratio) |

Plus three diagnostics: WiFi RSSI, IP address, SSID.

Plus desk presence (WiFi CSI, see below): Desk Presence, Desk Motion,
CSI Motion Level, and Presence / Motion Threshold numbers.

Derived values are estimates from population-average equations. They will
differ from the Eufy app (proprietary tuning) by 1–3 percentage points;
trend over time matters more than absolute calibration.

## How the protocol works (Eufy T9130)

Eufy bathroom scales broadcast measurement data in their BLE manufacturer-
data advertising payload. No GATT connection is required — anyone in range
can passively decode the frames. The scale only broadcasts while it's awake
(touch-wake on weight or impedance contact).

### Adv frame byte layout (after company ID)

| Pos | Bytes | Meaning |
|---|---|---|
| 0–4 | 5 | MAC suffix (constant) |
| 5 | 1 | `0x00` (separator) |
| 6 | 1 | Adv frame counter (per-second, rolls over) |
| 7 | 1 | Weigh-in counter (per-session, persists) |
| 8–9 | 2 | `0x02 0x6A` (constant) |
| 10 | 1 | **Status byte** — bit `0x80` = FINAL / SAVED (fires once per weigh-in) |
| 11 | 1 | Flags — `0x80` = extended trailer present |
| 12–13 | 2 | Weight LE ÷ 100 = kg |
| 14 | 1 | Display unit flag (`0x01` in our captures) |
| 15–16 | 2 | Heart rate LE (BPM) — populated only in final frame |
| 17–18 | 2 | Impedance LE ÷ 10 = Ω — populated when status bit `0x20` set |
| 19–21 | 3 | Session ID (random per weigh-in) |
| 22–23 | 2 | `0x00 0x00` (padding) |

The status byte progresses through the weigh-in lifecycle:
`0x35` (idle) → `0x01` (settling) → `0x15` → `0x25` (stable+impedance) →
`0x65` (impedance valid) → `0xA5` (final/saved, capture trigger).

## Desk presence (WiFi CSI)

The board sits on the desk, so it also works as a passive WiFi radar:
`wifi_csi.h` reads Channel State Information (per-subcarrier amplitude) from
every frame the AP sends, and a person moving near the TX→RX path changes the
multipath. The pipeline is WiFi-Sense's CSI path (moving average → variance →
integrator → EMA → on/off hysteresis), which itself builds on the
MotionDetector RSSI algorithm. Only frames from the connected AP's BSSID are
used. An idle station gets almost no CSI-bearing frames (beacons are legacy
DSSS, ~0.3 frames/s measured), so the firmware pings the gateway every
100 ms; each echo reply yields a CSI frame (~10/s).

Tuning: watch **CSI Motion Level** with the desk empty versus occupied, then
set **Presence Threshold** just above the empty baseline and **Motion
Threshold** above the level you see while sitting still. Both thresholds are
HA number entities and are restored across reboots.

Caveats: CSI picks up *movement*. Someone sitting very still can drop below
the threshold (the 15-tick off-hysteresis covers short pauses). The board is
mains-powered, so WiFi runs with `power_save_mode: none` (the radio hears
every AP frame, not just DTIM beacons) at max TX power (20.5 dB). The radio
is still time-shared with the BLE scan (30 ms of every 320 ms).


- **Controller**: Canaduino PLC + Arduino Nano ESP32 (ESP32-S3)
- **Scale**: Eufy T9130 (and probably the rest of the Eufy bathroom-scale
  family — T9140, T9148, P1 — though only T9130 is verified here)
- **Placement**: same room as the scale, RSSI typically ≥ −70 dBm

## Setup

1. Copy `secrets.example.h` to `secrets.h` and fill in all values (WiFi,
   MQTT, OTA, scale MAC, user profile).

2. To discover the scale's MAC: scan with nRF Connect (or any BLE scanner)
   while the scale is awake. Look for a device named `eufy T9130` (or
   similar `eufy …`). The MAC stays constant across reboots.

3. First flash: plug the Nano into USB. If the chip's USB descriptor is
   still in the Arduino MBED bootloader (`VID 0x2341`), you'll need to
   coax it into the ESP32-S3 ROM bootloader once:

   ```bash
   # ROM-mode entry: usb-reset switches the chip to download mode
   esptool --chip esp32s3 --port /dev/cu.usbmodemXXX --before usb-reset \
       --before no-reset --baud 921600 \
       write-flash 0x0 .esphome/build/scale-monitor/.pioenvs/scale-monitor/firmware.factory.bin
   ```

   After the first flash and a USB power-cycle, the device boots ESPHome
   normally. Subsequent flashes go over the air:

   ```bash
   ./upload.sh                  # uses scale-monitor.local
   ./upload.sh 172.24.1.x       # or by IP
   ```

4. To watch the raw BLE adv stream (useful for protocol debugging):

   ```bash
   ./mosquitto_logs.sh scale/logs
   ```

## Taking a weigh-in

1. Press the **"Weigh In"** button in HA (auto-discovered) or via MQTT:

   ```bash
   mosquitto_pub -h <broker> -u <user> -P <pass> \
       -t 'scale/button/weigh_in/command' -m PRESS
   ```

2. The onboard orange LED starts blinking.
3. Step on the scale. Once the scale's display locks and the LED goes
   solid for ~3 seconds, all sensors have been published. If 90 seconds
   elapse without a final frame, the firmware times out silently.

## Single user, single profile

The scale's adv doesn't carry user identity — you have to be the one on
the scale at the moment of capture. For multi-user setups you'd need
either (a) per-user firmware builds with different `USER_*` profiles, or
(b) an HA template helper that picks the active user before re-computing
derived metrics from the raw weight + impedance.

## Dashboard cards (Home Assistant)

Optional Lovelace cards and helpers live in `ha-cards/`. The cards need
`custom:button-card` and `custom:apexcharts-card` (both via HACS); the two
`template:` files are pasted into `configuration.yaml` (reload Template
Entities after).

| File | Type | Purpose |
|---|---|---|
| `scale-card.yaml` | Lovelace | Body-composition card (Beaver Orange theme) |
| `ha-current-weight-dashboard.yaml` | Lovelace | Full dashboard: card with previous reading under each value, Weigh In button, weight trend |
| `scale-card-previous.yaml` | Lovelace | Same layout showing the previous reading |
| `scale-weight-chart.yaml` | Lovelace | Weight trend (apexcharts, reads the `points` attribute) |
| `previous-readings.yaml` | `template:` | Trigger sensors snapshotting each metric's prior value (also on weigh-ins where it didn't change) |
| `weight-history.yaml` | `template:` | Trigger sensor accumulating `[epoch_ms, lb]` pairs for the trend |

`weight-history.yaml` relies on Heart Rate / Impedance / the body-comp
sensors carrying `state_class: measurement`, which also lets HA keep
long-term statistics for them.

## Files

| File | Purpose |
|---|---|
| `scale-monitor.yaml` | ESPHome config (BLE listener, decoder, body-comp math, LED state machine) |
| `wifi_csi.h` | WiFi CSI presence/motion detector (included by `scale-monitor.yaml`) |
| `ha-cards/` | HA Lovelace cards + template-sensor helpers (see "Dashboard cards") |
| `secrets.example.h` | template for `secrets.h` |
| `secrets.h` | actual values — **gitignored** |
| `upload.sh` | OTA helper that injects secrets as ESPHome `-s` substitutions |
| `mosquitto_logs.sh` | tail `scale/#` (or a sub-topic) over MQTT |
| `.gitignore` | excludes `secrets.h`, build artifacts, swap/backup files, image screenshots |

## License

MIT

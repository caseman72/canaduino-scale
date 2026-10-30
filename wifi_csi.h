#pragma once
// WiFi CSI presence/motion detector. The CSI path of WiFi-Sense's
// SignalProcessor (WiFi-Sense/src/wifi-sense/signal_processor.cpp), run from
// an ESPHome 100 ms interval instead of Arduino loop(). The RSSI path (the
// MotionDetector / bistatic-radar algorithm) is left out: it's the same
// variance pipeline fed one number per frame instead of ~64 subcarriers.

#include <cmath>
#include <cstring>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "ping/ping_sock.h"
#include "freertos/FreeRTOS.h"

namespace wifi_csi {

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t ap_bssid[6];
// Per-frame mean amplitudes accumulated by the WiFi task, drained each tick.
static float amp_sum = 0;
static int amp_count = 0;

static void on_csi(void *, wifi_csi_info_t *info) {
  if (!info || !info->buf || info->len < 4) return;
  // Only frames from our AP: one fixed TX→RX path, so variance means motion,
  // not a different transmitter.
  if (memcmp(info->mac, ap_bssid, 6) != 0) return;

  // I/Q int8 pairs; first 64 pairs (LLTF) as in WiFi-Sense's csi_collector.
  int pairs = info->len / 2;
  if (pairs > 64) pairs = 64;
  int start = info->first_word_invalid ? 2 : 0;
  float sum = 0;
  int n = 0;
  for (int i = start; i < pairs; i++) {
    float im = info->buf[i * 2], re = info->buf[i * 2 + 1];
    sum += sqrtf(im * im + re * re);
    n++;
  }
  if (n == 0) return;

  portENTER_CRITICAL(&mux);
  amp_sum += sum / n;
  amp_count++;
  portEXIT_CRITICAL(&mux);
}

// An idle station hears almost nothing from the AP that carries CSI (beacons
// go out at legacy DSSS rates with no LTF) — measured ~0.3 frames/s. Ping the
// gateway every 100 ms so each echo reply gives a CSI frame, as Espressif's
// esp-csi examples do.
static esp_ping_handle_t ping = nullptr;

inline bool start_ping() {
  if (ping) {
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
    ping = nullptr;
  }
  esp_netif_ip_info_t ip;
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!sta || esp_netif_get_ip_info(sta, &ip) != ESP_OK || ip.gw.addr == 0) return false;

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  ip_addr_set_ip4_u32(&cfg.target_addr, ip.gw.addr);
  cfg.count = ESP_PING_COUNT_INFINITE;
  cfg.interval_ms = 100;
  cfg.timeout_ms = 1000;
  cfg.data_size = 1;
  esp_ping_callbacks_t cbs = {};
  return esp_ping_new_session(&cfg, &cbs, &ping) == ESP_OK && esp_ping_start(ping) == ESP_OK;
}

// Call after WiFi connects (and on every reconnect — the BSSID may change).
inline bool start() {
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
  memcpy(ap_bssid, ap.bssid, 6);
  if (!start_ping()) return false;

  wifi_csi_config_t cfg = {};
  cfg.lltf_en = true;
  cfg.htltf_en = true;
  cfg.stbc_htltf2_en = true;
  cfg.ltf_merge_en = true;
  cfg.channel_filter_en = false;
  cfg.manu_scale = false;
  return esp_wifi_set_csi_config(&cfg) == ESP_OK &&
         esp_wifi_set_csi_rx_cb(on_csi, nullptr) == ESP_OK &&
         esp_wifi_set_csi(true) == ESP_OK;
}

struct Detector {
  // WiFi-Sense defaults: processor.init(64, 32, ..., 8), EMA 0.08, hysteresis 5/15.
  static const int BUF = 64, AVG = 32, INTEG = 8, ON = 5, OFF = 15;
  static constexpr float ALPHA = 0.08f;

  float avg_buf[BUF] = {};
  float var_buf[BUF] = {};
  int idx = 0, var_idx = 0;
  bool full = false;
  float ema = 0;
  int level = 0;
  int pres_on = 0, pres_off = 0, mot_on = 0, mot_off = 0;
  bool presence = false, motion = false;

  void tick(int presence_th, int motion_th) {
    portENTER_CRITICAL(&mux);
    float s = amp_sum;
    int n = amp_count;
    amp_sum = 0;
    amp_count = 0;
    portEXIT_CRITICAL(&mux);

    if (n > 0) process(s / n);

    // Hysteresis runs every tick, like WiFi-Sense's getResult().
    step(level > presence_th, pres_on, pres_off, presence);
    step(level > motion_th, mot_on, mot_off, motion);
  }

 private:
  void process(float amp) {
    avg_buf[idx] = amp;
    if (++idx >= BUF) { idx = 0; full = true; }
    if (!full) return;  // still filling (~6 s after boot)

    float mean = 0;
    for (int i = 0; i < AVG; i++) mean += avg_buf[(idx - 1 - i + BUF) % BUF];
    mean /= AVG;

    float dev = amp - mean;
    var_buf[var_idx] = dev * dev;
    if (++var_idx >= BUF) var_idx = 0;

    float integral = 0;
    for (int i = 0; i < INTEG; i++) integral += var_buf[(var_idx - 1 - i + BUF) % BUF];

    ema = ema * (1.0f - ALPHA) + integral * 10.0f * ALPHA;
    level = (int)ema;
  }

  static void step(bool raw, int &on, int &off, bool &state) {
    if (raw) {
      off = 0;
      if (++on >= ON) state = true;
    } else {
      on = 0;
      if (++off >= OFF) state = false;
    }
  }
};

static Detector detector;

}  // namespace wifi_csi

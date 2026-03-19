#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <Wire.h>
#include <U8g2lib.h>

// ── Board pins ────────────────────────────────────────────────────────────────
#define OLED_SCL  6
#define OLED_SDA  5
#define BOOT_BTN  9   // active LOW, internal pullup

// ── Config ────────────────────────────────────────────────────────────────────
#define CHAN_MIN       1
#define CHAN_MAX      13
#define CHAN_HOP_MS  500   // dwell per channel (longer = more captures per ch)

// ── Display ───────────────────────────────────────────────────────────────────
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ── Frame type counters ───────────────────────────────────────────────────────
static volatile uint32_t cnt_beacon   = 0;
static volatile uint32_t cnt_probe    = 0;
static volatile uint32_t cnt_deauth   = 0;
static volatile uint32_t cnt_data     = 0;
static volatile uint32_t cnt_other    = 0;
static volatile uint32_t cnt_total    = 0;

// ── Channel state ─────────────────────────────────────────────────────────────
static uint8_t  cur_chan    = CHAN_MIN;
static bool     hop_enabled = true;
static uint32_t chan_timer  = 0;

// ── 802.11 frame control subtypes ────────────────────────────────────────────
// Frame Control byte 0: bits[7:4] = subtype, bits[3:2] = type
// type 00 = management, type 10 = data
#define FC_TYPE(fc0)     (((fc0) >> 2) & 0x03)
#define FC_SUBTYPE(fc0)  (((fc0) >> 4) & 0x0F)

#define TYPE_MGMT  0x00
#define TYPE_DATA  0x02
#define SUB_BEACON 0x08
#define SUB_PROBE_REQ  0x04
#define SUB_PROBE_RESP 0x05
#define SUB_DEAUTH 0x0C

// ── Promiscuous callback ──────────────────────────────────────────────────────
static void IRAM_ATTR promisc_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type == WIFI_PKT_MISC) return;

  wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  if (pkt->rx_ctrl.sig_len < 2) return;

  uint8_t fc0      = pkt->payload[0];
  uint8_t pkt_type = FC_TYPE(fc0);
  uint8_t subtype  = FC_SUBTYPE(fc0);

  cnt_total++;

  if (pkt_type == TYPE_MGMT) {
    switch (subtype) {
      case SUB_BEACON:                cnt_beacon++;  break;
      case SUB_PROBE_REQ:
      case SUB_PROBE_RESP:            cnt_probe++;   break;
      case SUB_DEAUTH:                cnt_deauth++;  break;
      default:                        cnt_other++;   break;
    }
  } else if (pkt_type == TYPE_DATA) {
    cnt_data++;
  } else {
    cnt_other++;
  }

  // Serial log every frame with RSSI — useful for your RF data
  // Comment out if serial output is too noisy
  Serial.printf("ch%2d  RSSI:%4d  type:%d sub:%02X  tot:%lu\n",
    pkt->rx_ctrl.channel,
    pkt->rx_ctrl.rssi,
    pkt_type, subtype,
    cnt_total);
}

// ── OLED update ───────────────────────────────────────────────────────────────
static void oled_update() {
  char l1[20], l2[20], l3[20], l4[20];
  snprintf(l1, sizeof(l1), "ch%d %s", cur_chan, hop_enabled ? "HOP" : "LOCK");
  snprintf(l2, sizeof(l2), "BCN:%lu PRB:%lu", cnt_beacon, cnt_probe);
  snprintf(l3, sizeof(l3), "DAT:%lu DTH:%lu", cnt_data, cnt_deauth);
  snprintf(l4, sizeof(l4), "TOT:%lu", cnt_total);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(0, 6,  l1);
  u8g2.drawStr(0, 14, l2);
  u8g2.drawStr(0, 22, l3);
  u8g2.drawStr(0, 30, l4);
  u8g2.sendBuffer();
}

// ── Button (debounced) ────────────────────────────────────────────────────────
// Short press = toggle hop lock / manual channel advance
// Long press  = reset counters
static uint8_t read_button() {
  if (digitalRead(BOOT_BTN) != LOW) return 0;
  delay(30);
  if (digitalRead(BOOT_BTN) != LOW) return 0;
  uint32_t t = millis();
  while (digitalRead(BOOT_BTN) == LOW) {
    if (millis() - t > 1500) {
      while (digitalRead(BOOT_BTN) == LOW) delay(10);
      return 2; // long press
    }
    delay(10);
  }
  return 1; // short press
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(0, 6,  "SNIFFER");
  u8g2.drawStr(0, 14, "STARTING...");
  u8g2.sendBuffer();

  // Init WiFi in null mode, then enable promiscuous
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_wifi_start();

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(promisc_cb);
  esp_wifi_set_channel(cur_chan, WIFI_SECOND_CHAN_NONE);

  chan_timer = millis();
  oled_update();

  Serial.println("[*] Promiscuous sniffer active");
  Serial.println("    Short press = lock/unlock channel hop");
  Serial.println("    Long press  = reset counters");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  uint8_t btn = read_button();

  if (btn == 1) {
    if (hop_enabled) {
      // Lock on current channel
      hop_enabled = false;
      Serial.printf("[*] Locked on ch%d\n", cur_chan);
    } else {
      // Manual advance when locked
      cur_chan = (cur_chan >= CHAN_MAX) ? CHAN_MIN : cur_chan + 1;
      esp_wifi_set_channel(cur_chan, WIFI_SECOND_CHAN_NONE);
      Serial.printf("[>] Manual ch%d\n", cur_chan);
    }
    oled_update();
  } else if (btn == 2) {
    // Reset all counters
    cnt_beacon = cnt_probe = cnt_deauth = cnt_data = cnt_other = cnt_total = 0;
    hop_enabled = true;
    Serial.println("[*] Counters reset, hop enabled");
    oled_update();
  }

  // Channel hopping
  if (hop_enabled && (millis() - chan_timer >= CHAN_HOP_MS)) {
    cur_chan = (cur_chan >= CHAN_MAX) ? CHAN_MIN : cur_chan + 1;
    esp_wifi_set_channel(cur_chan, WIFI_SECOND_CHAN_NONE);
    chan_timer = millis();
  }

  // Refresh OLED ~4x/sec
  static uint32_t last_oled = 0;
  if (millis() - last_oled >= 250) {
    oled_update();
    last_oled = millis();
  }
}

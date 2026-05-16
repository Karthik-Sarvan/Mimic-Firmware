# BMP280 SPI Sensor Emulation Guide

This document provides specific instructions for simulating the **Bosch BMP280** Pressure & Temperature sensor via SPI.

## 📡 Protocol Details
- **Interface**: SPI (Slave Mode)
- **SPI Mode**: Mode 0 (CPOL=0, CPHA=0)
- **Address Range**: 0x00 - 0x7F (7-bit internally)
- **Chip ID**: 0x58

## 🔌 Connection Diagram (ESP8266 to STM32)

| NodeMCU Pin | STM32 Pin | BMP280 Signal | Description |
|-------------|-----------|---------------|-------------|
| **D5**      | **PA5**   | **SCK**       | Serial Clock |
| **D6**      | **PA6**   | **MISO**      | Master In / Slave Out |
| **D7**      | **PA7**   | **MOSI**      | Master Out / Slave In |
| **D8**      | **PA4**   | **CS**        | Chip Select (Active Low) |
| **GND**     | **GND**   | **GND**       | Common Ground |

## 🧠 Emulation Logic
The Mimic firmware uses a **"Chambered Response"** strategy to beat the high-speed SPI clock:
1. **Idle State**: The STM32 pre-loads the Chip ID (`0x58`) into its SPI data register.
2. **Start of Transaction**: When the Master (ESP8266) pulls CS Low, the ID is ready to be shifted out instantly.
3. **Burst Reads**: The STM32 auto-increments the register address during continuous clocking, allowing the master to read Calibration and Temperature data in a single burst.

## 🛠️ Testing with Arduino
Use the standard `Adafruit_BMP280` library with the following constructor:

```cpp
#include <Adafruit_BMP280.h>

#define BMP_SCK  14
#define BMP_MISO 12
#define BMP_MOSI 13
#define BMP_CS   15

Adafruit_BMP280 bmp(BMP_CS, BMP_MOSI, BMP_MISO, BMP_SCK);

void setup() {
  if (!bmp.begin()) {
    Serial.println("Could not find a valid BMP280 sensor!");
    while (1);
  }
}
```

## 🔍 Troubleshooting
- **No Sensor Detected**: Check the CS line (PA4). If it's floating or not connected to D8, the STM32 won't start the SPI handler.
- **Values are 0.00**: Ensure the `mimic` CLI is running on the PC. The STM32 needs the Python script to feed it the calibration values.
- **Heartbeat Check**: The STM32's onboard LED (PC13) will blink rapidly during active SPI transactions.

---
*Mimic Project - SPI Protocol Documentation*

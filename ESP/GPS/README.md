# GPS UART (NEO-6M) Sensor Emulation Guide

This document details the simulation of a **NEO-6M GPS Module** sending real-time NMEA sentences via UART.

## 📡 Protocol Details
- **Interface**: UART (Asynchronous)
- **Baud Rate**: 9600 (Standard for GPS)
- **Data Bits**: 8
- **Stop Bits**: 1
- **Parity**: None
- **Update Rate**: 1Hz (1 sentence per second)

## 🔌 Connection Diagram (ESP8266 to STM32)

| ESP8266 (NodeMCU) | STM32 Pin | GPS Signal | Description |
|-------------|-----------|------------|-------------|
| **D7**      | **PA11**  | **TX6**    | GPS Data Output (from STM32) |
| **D8**      | **PA12**  | **RX6**    | GPS Data Input (to STM32) |
| **GND**     | **GND**   | **GND**    | Common Ground |

## 🧠 Emulation Logic
The Mimic firmware generates **NMEA 0183** sentences, specifically the `$GPRMC` (Recommended Minimum) sentence:
1. **Sentence Generation**: The Python script calculates a "walking" latitude and longitude starting from Bangalore, India.
2. **Checksum**: Every sentence includes a bitwise XOR checksum at the end (e.g., `*6A`) to ensure data integrity.
3. **UART Bridge**: The STM32 receives these strings from the PC via its main USB-UART and forwards them to **UART1 (PA9)** at 9600 baud.

### Example Sentence:
`$GPRMC,123519,A,12.9716,N,77.5946,E,0.0,0.0,280426,,,A*6A`

## 🛠️ Testing with Arduino
You can use the provided `GPS.ino` which utilizes `SoftwareSerial`. This allows you to read the GPS data on pins D7/D8 while using the main USB port for the Serial Monitor.

```cpp
#include <SoftwareSerial.h>
SoftwareSerial gpsSerial(13, 15); // RX=D7, TX=D8

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600); // Must match Mimic baud rate
}
```

## 🔍 Troubleshooting
- **Garbage Characters**: Ensure the ESP8266 baud rate is set to **9600**. If it's 115200, the data will be unreadable.
- **No Data**: Verify that you are connected to **PA9** (TX) on the STM32. Remember, the STM32's TX goes to the ESP8266's RX!
- **Checksum Errors**: If you are writing your own parser, ensure you handle the `*XX` checksum at the end of each NMEA string.

---
*Mimic Project - UART Protocol Documentation*

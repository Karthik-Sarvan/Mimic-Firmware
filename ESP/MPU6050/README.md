# MPU6050 I2C Sensor Emulation Guide

This document provides specific instructions for simulating the **InvenSense MPU6050** 6-Axis Accelerometer & Gyroscope via I2C.

## 📡 Protocol Details
- **Interface**: I2C (Slave Mode)
- **Clock Speed**: 100kHz (Standard Mode) or 400kHz (Fast Mode)
- **Slave Address**: 0x68 (Default)
- **WhoAmI Value**: 0x68 (Register 0x75)

## 🔌 Connection Diagram (ESP8266 to STM32)

| NodeMCU Pin | STM32 Pin | MPU6050 Signal | Description |
|-------------|-----------|----------------|-------------|
| **D1**      | **PB6**   | **SCL**        | I2C Clock |
| **D2**      | **PB7**   | **SDA**        | I2C Data |
| **GND**     | **GND**   | **GND**        | Common Ground |

> [!TIP]
> If using long wires (>10cm), add 4.7k ohm pull-up resistors from SCL/SDA to 3.3V to ensure sharp signal edges.

## 🧠 Emulation Logic
The Mimic firmware handles I2C via hardware interrupts:
1. **Address Match**: The STM32 triggers an interrupt when it sees the `0x68` address on the bus.
2. **Register Map**: Data is stored in a 256-byte internal map. The ESP8266 reads Accel/Gyro data from registers `0x3B` to `0x48`.
3. **Diagnostic Blink**: The onboard LED (PC13) will toggle on every I2C event, providing instant visual feedback that the ESP8266 is communicating.

## 🛠️ Testing with Arduino
Use the standard `Adafruit_MPU6050` library. Note that no special pin definitions are needed as it uses the default I2C pins.

```cpp
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

void setup() {
  // Initialize I2C (D1/D2 on ESP8266)
  Wire.begin(4, 5); 

  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1);
  }
}
```

## 🔍 Troubleshooting
- **Sensor Not Detected**: Check if the STM32 LED blinks when you reset the ESP8266. If it blinks but the library says "Not Found," the I2C address might be mismatched (check if it's `0x68` or `0x69`).
- **I2C Timeout**: This usually indicates a lack of pull-up resistors or a loose ground wire.
- **Garbage Data**: Ensure the `mimic` CLI is running and active.

---
*Mimic Project - I2C Protocol Documentation*

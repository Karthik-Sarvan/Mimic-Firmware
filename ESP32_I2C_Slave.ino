#include <Wire.h>

/*
 * ESP32 I2C Slave Test Firmware for Mimic
 * 
 * Configures ESP32 as an I2C slave to test STM32 I2C master capabilities.
 * 
 * Wiring:
 * - STM32 PB6  (I2C1 SCL) -> ESP32 GPIO 22 (SCL)
 * - STM32 PB7  (I2C1 SDA) -> ESP32 GPIO 21 (SDA)
 * - GND -> GND
 * 
 * Note: Internal pull-ups are used, but external 4.7k resistors recommended for stability.
 */

#define I2C_SLAVE_ADDR 0x55
#define SDA_PIN 21
#define SCL_PIN 22

// Buffers
volatile uint8_t registers[256]; // Simulate 256 registers
volatile uint8_t last_reg_addr = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== ESP32 I2C Slave ===");
  Serial.printf("I2C Address: 0x%02X\n", I2C_SLAVE_ADDR);
  Serial.printf("SDA: GPIO %d\n", SDA_PIN);
  Serial.printf("SCL: GPIO %d\n", SCL_PIN);
  
  // Initialize 'registers' with some known data
  for (int i = 0; i < 256; i++) {
    registers[i] = i; // Reg 0x05 contains 0x05, etc.
  }
  registers[0x00] = 0xBE; // Magic byte 1
  registers[0x01] = 0xEF; // Magic byte 2
  
  // Initialize I2C as Slave
  // valid for ESP32 Arduino core: begin(addr, sda, scl, freq)
  bool success = Wire.begin((uint8_t)I2C_SLAVE_ADDR, SDA_PIN, SCL_PIN, 100000);
  
  if (!success) {
    Serial.println("ERROR: Failed to initialize I2C!");
    while(1) delay(100);
  }
  
  Wire.onReceive(onReceiveEvent);
  Wire.onRequest(onRequestEvent);
  
  Serial.println("Ready! Waiting for I2C commands...");
}

void loop() {
  delay(100);
}

// Called when master writes data
void onReceiveEvent(int howMany) {
  Serial.printf("[RX] Received %d bytes: ", howMany);
  
  if (howMany > 0) {
    // First byte is the register address
    last_reg_addr = Wire.read();
    Serial.printf("[%02X] ", last_reg_addr);
    howMany--;
    
    // Remaining bytes are data written to that register and subsequent ones
    while (Wire.available()) {
      uint8_t data = Wire.read();
      Serial.printf("%02X ", data);
      registers[last_reg_addr++] = data; // Auto-increment address
    }
  }
  Serial.println();
}

// Called when master reads data
void onRequestEvent() {
  // Return data from the current register address
  uint8_t data = registers[last_reg_addr];
  Wire.write(data);
  
  Serial.printf("[TX] Sent register [0x%02X]: 0x%02X\n", last_reg_addr, data);
  
  last_reg_addr++; // Auto-increment for next read
}

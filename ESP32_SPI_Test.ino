/**
 * ESP32 SPI Slave Test for STM32 Mimic Firmware
 * 
 * This sketch configures ESP32 as an SPI slave to test all SPI modes
 * with the STM32F411 Mimic firmware.
 * 
 * Supports:
 * - All 4 SPI modes (0, 1, 2, 3)
 * - Full-duplex communication
 * - Echo mode and counter mode
 * - Serial monitor for debugging
 */

#include <SPI.h>

// ESP32 SPI Slave pins (VSPI)
#define MISO_PIN    19
#define MOSI_PIN    23
#define SCK_PIN     18
#define CS_PIN      5

// SPI Configuration
SPIClass * vspi = NULL;
static const int spiClk = 1000000; // 1 MHz

// Data buffers
#define BUFFER_SIZE 64
uint8_t txBuffer[BUFFER_SIZE];
uint8_t rxBuffer[BUFFER_SIZE];
volatile uint8_t dataReceived = 0;
volatile uint8_t counter = 0;

// Mode selection
int currentMode = 0;  // 0, 1, 2, or 3
bool echoMode = true; // true = echo back, false = send counter

// Statistics
unsigned long totalTransfers = 0;
unsigned long lastPrintTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== ESP32 SPI Slave Test ===");
  Serial.println("For STM32F411 Mimic Firmware");
  Serial.println("=============================\n");
  
  // Initialize SPI slave
  setupSPISlave(currentMode);
  
  printMenu();
}

void setupSPISlave(int mode) {
  // Determine SPI mode settings
  uint8_t spiMode;
  switch(mode) {
    case 0: spiMode = SPI_MODE0; break; // CPOL=0, CPHA=0
    case 1: spiMode = SPI_MODE1; break; // CPOL=0, CPHA=1
    case 2: spiMode = SPI_MODE2; break; // CPOL=1, CPHA=0
    case 3: spiMode = SPI_MODE3; break; // CPOL=1, CPHA=1
    default: spiMode = SPI_MODE0;
  }
  
  // Configure pins
  pinMode(CS_PIN, INPUT);
  pinMode(MISO_PIN, OUTPUT);
  pinMode(MOSI_PIN, INPUT);
  pinMode(SCK_PIN, INPUT);
  
  // Initialize VSPI
  vspi = new SPIClass(VSPI);
  vspi->begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
  
  // Set as slave
  vspi->beginTransaction(SPISettings(spiClk, MSBFIRST, spiMode));
  
  Serial.printf("SPI Slave initialized in Mode %d\n", mode);
  Serial.printf("  CPOL=%d, CPHA=%d\n", (mode >> 1) & 1, mode & 1);
  Serial.printf("  Echo Mode: %s\n", echoMode ? "ON" : "OFF (Counter)");
  Serial.println();
}

void printMenu() {
  Serial.println("Commands:");
  Serial.println("  0-3: Switch to SPI Mode 0-3");
  Serial.println("  e: Toggle Echo/Counter mode");
  Serial.println("  s: Show statistics");
  Serial.println("  r: Reset counter");
  Serial.println("  h: Show this help");
  Serial.println();
}

void loop() {
  // Check for serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    handleCommand(cmd);
  }
  
  // Check if CS is LOW (active)
  if (digitalRead(CS_PIN) == LOW) {
    handleSPITransfer();
  }
  
  // Print statistics every 2 seconds
  if (millis() - lastPrintTime > 2000 && totalTransfers > 0) {
    printStats();
    lastPrintTime = millis();
  }
}

void handleSPITransfer() {
  uint8_t receivedByte;
  uint8_t sendByte;
  
  // Wait for CS to go LOW
  while (digitalRead(CS_PIN) == LOW) {
    // Transfer one byte
    receivedByte = vspi->transfer(echoMode ? 0x00 : counter);
    
    if (receivedByte != 0x00) {
      rxBuffer[dataReceived] = receivedByte;
      dataReceived++;
      
      if (echoMode) {
        // In echo mode, send back what we received
        vspi->transfer(receivedByte);
      } else {
        // In counter mode, increment counter
        counter++;
      }
      
      totalTransfers++;
    }
    
    // Small delay to prevent tight loop
    delayMicroseconds(10);
  }
  
  // Process received data
  if (dataReceived > 0) {
    Serial.print("RX: ");
    for (int i = 0; i < dataReceived; i++) {
      Serial.printf("%02X ", rxBuffer[i]);
    }
    Serial.println();
    dataReceived = 0;
  }
}

void handleCommand(char cmd) {
  switch(cmd) {
    case '0':
    case '1':
    case '2':
    case '3':
      currentMode = cmd - '0';
      vspi->end();
      setupSPISlave(currentMode);
      Serial.printf("Switched to SPI Mode %d\n\n", currentMode);
      break;
      
    case 'e':
    case 'E':
      echoMode = !echoMode;
      Serial.printf("Echo mode: %s\n\n", echoMode ? "ON" : "OFF (Counter)");
      break;
      
    case 's':
    case 'S':
      printStats();
      break;
      
    case 'r':
    case 'R':
      counter = 0;
      totalTransfers = 0;
      Serial.println("Counter and stats reset\n");
      break;
      
    case 'h':
    case 'H':
      printMenu();
      break;
      
    case '\n':
    case '\r':
      // Ignore newlines
      break;
      
    default:
      Serial.printf("Unknown command: %c\n", cmd);
      printMenu();
  }
}

void printStats() {
  Serial.println("\n--- Statistics ---");
  Serial.printf("Mode: %d (CPOL=%d, CPHA=%d)\n", 
                currentMode, (currentMode >> 1) & 1, currentMode & 1);
  Serial.printf("Total transfers: %lu\n", totalTransfers);
  Serial.printf("Counter value: %d\n", counter);
  Serial.printf("Echo mode: %s\n", echoMode ? "ON" : "OFF");
  Serial.println("------------------\n");
}

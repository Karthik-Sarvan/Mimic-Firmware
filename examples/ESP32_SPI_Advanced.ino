/**
 * ESP32 Advanced SPI Slave for STM32 Testing
 * 
 * This version uses ESP32's hardware SPI slave peripheral
 * for more reliable communication and better performance.
 * 
 * Features:
 * - Hardware SPI slave using DMA
 * - All 4 SPI modes
 * - Multiple test patterns
 * - Automatic response generation
 */

#include "driver/spi_slave.h"
#include "driver/gpio.h"

// SPI pins for VSPI
#define GPIO_MOSI 23
#define GPIO_MISO 19
#define GPIO_SCLK 18
#define GPIO_CS   5

// DMA buffer size
#define BUFFER_SIZE 64

// SPI slave handle
spi_slave_transaction_t t;
__attribute__((aligned(4))) uint8_t sendbuf[BUFFER_SIZE];
__attribute__((aligned(4))) uint8_t recvbuf[BUFFER_SIZE];

// Test modes
enum TestMode {
  MODE_ECHO,      // Echo back received data
  MODE_COUNTER,   // Send incrementing counter
  MODE_PATTERN,   // Send test pattern
  MODE_RANDOM     // Send random data
};

TestMode currentTestMode = MODE_ECHO;
int spiMode = 0;
uint8_t counter = 0;
unsigned long transferCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== ESP32 Advanced SPI Slave ===");
  Serial.println("Hardware SPI with DMA");
  Serial.println("================================\n");
  
  setupSPISlave(spiMode);
  printMenu();
}

void setupSPISlave(int mode) {
  // Configuration for the SPI slave
  spi_bus_config_t buscfg = {
    .mosi_io_num = GPIO_MOSI,
    .miso_io_num = GPIO_MISO,
    .sclk_io_num = GPIO_SCLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = BUFFER_SIZE,
  };
  
  spi_slave_interface_config_t slvcfg = {
    .spics_io_num = GPIO_CS,
    .flags = 0,
    .queue_size = 3,
    .mode = mode,
    .post_setup_cb = NULL,
    .post_trans_cb = NULL
  };
  
  // Enable pull-ups on SPI lines
  gpio_set_pull_mode((gpio_num_t)GPIO_MOSI, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)GPIO_SCLK, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)GPIO_CS, GPIO_PULLUP_ONLY);
  
  // Initialize SPI slave
  ESP_ERROR_CHECK(spi_slave_initialize(VSPI_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));
  
  Serial.printf("SPI Slave Mode %d initialized\n", mode);
  Serial.printf("  CPOL=%d, CPHA=%d\n", (mode >> 1) & 1, mode & 1);
  Serial.println();
}

void prepareTransmitData() {
  switch(currentTestMode) {
    case MODE_ECHO:
      // Data will be filled after receiving
      memset(sendbuf, 0x00, BUFFER_SIZE);
      break;
      
    case MODE_COUNTER:
      for (int i = 0; i < BUFFER_SIZE; i++) {
        sendbuf[i] = counter++;
      }
      break;
      
    case MODE_PATTERN:
      // Alternating pattern
      for (int i = 0; i < BUFFER_SIZE; i++) {
        sendbuf[i] = (i % 2) ? 0xAA : 0x55;
      }
      break;
      
    case MODE_RANDOM:
      for (int i = 0; i < BUFFER_SIZE; i++) {
        sendbuf[i] = random(0, 256);
      }
      break;
  }
}

void loop() {
  // Check for serial commands
  if (Serial.available()) {
    handleSerialCommand();
  }
  
  // Prepare transmit buffer based on mode
  prepareTransmitData();
  
  // Clear receive buffer
  memset(recvbuf, 0, BUFFER_SIZE);
  
  // Set up transaction
  memset(&t, 0, sizeof(t));
  t.length = BUFFER_SIZE * 8; // Length in bits
  t.tx_buffer = sendbuf;
  t.rx_buffer = recvbuf;
  
  // Queue transaction
  esp_err_t ret = spi_slave_queue_trans(VSPI_HOST, &t, portMAX_DELAY);
  assert(ret == ESP_OK);
  
  // Wait for transaction to complete
  spi_slave_transaction_t *rtrans;
  ret = spi_slave_get_trans_result(VSPI_HOST, &rtrans, portMAX_DELAY);
  assert(ret == ESP_OK);
  
  // Process received data
  if (rtrans->trans_len > 0) {
    int bytesReceived = rtrans->trans_len / 8;
    transferCount++;
    
    Serial.printf("Transfer #%lu: %d bytes\n", transferCount, bytesReceived);
    
    // Print received data
    Serial.print("  RX: ");
    for (int i = 0; i < bytesReceived && i < 16; i++) {
      Serial.printf("%02X ", recvbuf[i]);
    }
    if (bytesReceived > 16) Serial.print("...");
    Serial.println();
    
    // Print transmitted data
    Serial.print("  TX: ");
    for (int i = 0; i < bytesReceived && i < 16; i++) {
      Serial.printf("%02X ", sendbuf[i]);
    }
    if (bytesReceived > 16) Serial.print("...");
    Serial.println();
    
    // In echo mode, copy received to send buffer for next transaction
    if (currentTestMode == MODE_ECHO) {
      memcpy(sendbuf, recvbuf, BUFFER_SIZE);
    }
  }
}

void handleSerialCommand() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  
  if (cmd.length() == 0) return;
  
  char c = cmd.charAt(0);
  
  switch(c) {
    case '0':
    case '1':
    case '2':
    case '3':
      spiMode = c - '0';
      spi_slave_free(VSPI_HOST);
      setupSPISlave(spiMode);
      Serial.printf("Switched to SPI Mode %d\n\n", spiMode);
      break;
      
    case 'e':
      currentTestMode = MODE_ECHO;
      Serial.println("Test mode: ECHO\n");
      break;
      
    case 'c':
      currentTestMode = MODE_COUNTER;
      counter = 0;
      Serial.println("Test mode: COUNTER\n");
      break;
      
    case 'p':
      currentTestMode = MODE_PATTERN;
      Serial.println("Test mode: PATTERN (0x55/0xAA)\n");
      break;
      
    case 'r':
      currentTestMode = MODE_RANDOM;
      Serial.println("Test mode: RANDOM\n");
      break;
      
    case 's':
      printStats();
      break;
      
    case 'x':
      counter = 0;
      transferCount = 0;
      Serial.println("Reset counters\n");
      break;
      
    case 'h':
      printMenu();
      break;
      
    default:
      Serial.println("Unknown command\n");
      printMenu();
  }
}

void printMenu() {
  Serial.println("Commands:");
  Serial.println("  0-3: Switch SPI Mode");
  Serial.println("  e: Echo mode");
  Serial.println("  c: Counter mode");
  Serial.println("  p: Pattern mode (0x55/0xAA)");
  Serial.println("  r: Random mode");
  Serial.println("  s: Show statistics");
  Serial.println("  x: Reset counters");
  Serial.println("  h: Show help");
  Serial.println();
}

void printStats() {
  Serial.println("\n=== Statistics ===");
  Serial.printf("SPI Mode: %d (CPOL=%d, CPHA=%d)\n", 
                spiMode, (spiMode >> 1) & 1, spiMode & 1);
  Serial.printf("Test Mode: ");
  switch(currentTestMode) {
    case MODE_ECHO: Serial.println("ECHO"); break;
    case MODE_COUNTER: Serial.println("COUNTER"); break;
    case MODE_PATTERN: Serial.println("PATTERN"); break;
    case MODE_RANDOM: Serial.println("RANDOM"); break;
  }
  Serial.printf("Total Transfers: %lu\n", transferCount);
  Serial.printf("Counter Value: %d\n", counter);
  Serial.println("==================\n");
}

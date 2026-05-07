#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

#define BMP_SCK  14 // D5
#define BMP_MISO 12 // D6
#define BMP_MOSI 13 // D7
#define BMP_CS   15 // D8

Adafruit_BMP280 bmp(BMP_CS); 

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- BMP280 100kHz SPI Test ---");

  SPI.begin();
  
  // Use a very slow clock (100kHz) to ensure STM32 can keep up
  bool status = false;
  while (!status) {
    status = bmp.begin(100000); 
    if (!status) {
      Serial.println("Waiting for sensor...");
      delay(500);
    }
  }

  Serial.println("BMP280 Detected Successfully!");
}

void loop() {
  Serial.print("Temp: ");
  Serial.print(bmp.readTemperature());
  Serial.print(" C, Pressure: ");
  Serial.print(bmp.readPressure());
  Serial.println(" Pa");
  delay(1000);
}

#include <SoftwareSerial.h>

// GPS RX is D7 (GPIO13) on ESP8266
// Connect this to PA9 (TX1) on STM32
SoftwareSerial gpsSerial(13, 15); // RX, TX

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600); // Standard GPS Baud Rate
  
  Serial.println("\n--- GPS UART Test ---");
  Serial.println("Waiting for NMEA sentences from Mimic...");
}

void loop() {
  // Just bridge the data from GPS to PC Serial
  if (gpsSerial.available()) {
    char c = gpsSerial.read();
    Serial.write(c);
    
    // If we see the start of a sentence, blink the LED
    if (c == '$') {
      digitalWrite(LED_BUILTIN, LOW); // Blink ON
      delay(10);
      digitalWrite(LED_BUILTIN, HIGH); // Blink OFF
    }
  }
}

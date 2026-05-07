#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

void setup(void) {
  Serial.begin(115200);
  
  /* Standard MPU6050 Initialization */
  if (!mpu.begin()) {
    Serial.println("Error: MPU6050 sensor not detected on I2C bus!");
    while (1) delay(10);
  }
  
  // Set standard sensor ranges
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("MPU6050 Sensor Initialized [8G / 500DEG]");
  Serial.println("Reading streaming data...");
}

void loop() {
  sensors_event_t a, g, temp;
  
  /* Production-grade data acquisition */
  if (mpu.getEvent(&a, &g, &temp)) {
    Serial.print("DATA >> Accel[X,Y,Z]: ");
    Serial.print(a.acceleration.x, 2); Serial.print(", ");
    Serial.print(a.acceleration.y, 2); Serial.print(", ");
    Serial.print(a.acceleration.z, 2);
    Serial.print(" m/s^2 | Temp: ");
    Serial.print(temp.temperature, 1);
    Serial.println(" degC");
  } else {
    Serial.println("Bus Error: Communication lost!");
  }

  delay(200);
}

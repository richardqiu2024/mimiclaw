/*
  BMI270 Test using BMI270_AUX_BMM150 library

  Hardware: ESP32-S3
  I2C: SDA=GPIO2, SCL=GPIO1
*/

#include "BMI270_AUX_BMM150.h"

// Custom I2C pins for ESP32-S3
#define I2C_SDA 2
#define I2C_SCL 1

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("BMI270 Test with BMI270_AUX_BMM150 library");

  // Initialize I2C with custom pins
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  // Initialize IMU
  if (!IMU.begin(&Serial)) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

  Serial.println("IMU initialized successfully!");
  Serial.print("Accelerometer sample rate = ");
  Serial.print(IMU.accelerationSampleRate());
  Serial.println(" Hz");
  Serial.println();
  Serial.println("Acceleration in G's");
  Serial.println("X\tY\tZ");
}

void loop() {
  float x, y, z;

  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(x, y, z);

    Serial.print(x);
    Serial.print('\t');
    Serial.print(y);
    Serial.print('\t');
    Serial.println(z);
  }

  delay(100);
}

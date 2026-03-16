/*
  BMI270 Simple Accelerometer Test

  This example reads the acceleration values from the BMI270
  sensor and continuously prints them to the Serial Monitor.

  Based on BMI270_AUX_BMM150 library example
*/

#include <Wire.h>

// BMI270 I2C addresses
#define BMI270_I2C_ADDR_PRIMARY   0x68
#define BMI270_I2C_ADDR_SECONDARY 0x69

// I2C pins for ESP32-S3
#define I2C_SDA 2
#define I2C_SCL 1

// BMI270 registers
#define BMI270_CHIP_ID_REG    0x00
#define BMI270_CHIP_ID        0x24
#define BMI270_ACC_X_LSB      0x0C
#define BMI270_STATUS_REG     0x03

uint8_t bmi270_address = BMI270_I2C_ADDR_PRIMARY;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("BMI270 Arduino Test Starting...");

  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000); // 400kHz

  // Probe for BMI270
  Serial.println("Probing for BMI270...");

  if (probeBMI270(BMI270_I2C_ADDR_PRIMARY)) {
    bmi270_address = BMI270_I2C_ADDR_PRIMARY;
    Serial.printf("BMI270 found at 0x%02X\n", bmi270_address);
  } else if (probeBMI270(BMI270_I2C_ADDR_SECONDARY)) {
    bmi270_address = BMI270_I2C_ADDR_SECONDARY;
    Serial.printf("BMI270 found at 0x%02X\n", bmi270_address);
  } else {
    Serial.println("BMI270 not found!");
    while (1) {
      delay(1000);
    }
  }

  // Read chip ID
  uint8_t chip_id = readRegister(BMI270_CHIP_ID_REG);
  Serial.printf("Chip ID: 0x%02X (expected 0x24)\n", chip_id);

  if (chip_id != BMI270_CHIP_ID) {
    Serial.println("Invalid chip ID!");
    while (1) {
      delay(1000);
    }
  }

  Serial.println("BMI270 detected successfully!");
  Serial.println("Note: Full initialization requires config file upload");
  Serial.println("This simple test only reads chip ID and checks I2C communication");
}

void loop() {
  // Read status register
  uint8_t status = readRegister(BMI270_STATUS_REG);
  Serial.printf("Status: 0x%02X\n", status);

  delay(1000);
}

bool probeBMI270(uint8_t address) {
  Wire.beginTransmission(address);
  uint8_t error = Wire.endTransmission();
  return (error == 0);
}

uint8_t readRegister(uint8_t reg) {
  Wire.beginTransmission(bmi270_address);
  Wire.write(reg);
  Wire.endTransmission(false);

  Wire.requestFrom(bmi270_address, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0;
}

void writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(bmi270_address);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

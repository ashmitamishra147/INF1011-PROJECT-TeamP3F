
#include <Wire.h>
#include <SPI.h>
#include <Wireling.h>

#define SENSOR_ADDR 0x30
#define SENSOR_PORT 0

// Compatible with TinyCircuits SAMD processors
#if defined(ARDUINO_ARCH_AVR)
  #define SerialMonitorInterface Serial
#elif defined(ARDUINO_ARCH_SAMD)
  #define SerialMonitorInterface SerialUSB
#else
  #define SerialMonitorInterface Serial
#endif

// Check if sensor acknowledges I2C address
bool checkI2C() {
  Wire.beginTransmission(SENSOR_ADDR);
  byte error = Wire.endTransmission();

  SerialMonitorInterface.print("I2C ACK: ");
  SerialMonitorInterface.println(error == 0 ? "YES" : "NO");

  SerialMonitorInterface.print("I2C Error Code: ");
  SerialMonitorInterface.println(error);

  return error == 0;
}

// Read a 16-bit register directly
uint16_t readRegister(byte reg) {
  Wire.beginTransmission(SENSOR_ADDR);
  Wire.write(reg);

  byte error = Wire.endTransmission();

  if (error != 0) {
    SerialMonitorInterface.print("Write error: ");
    SerialMonitorInterface.println(error);
    return 0xFFFF;
  }

  delay(10);

  byte received = Wire.requestFrom(SENSOR_ADDR, 2);

  if (received != 2 || Wire.available() < 2) {
    SerialMonitorInterface.println("Read incomplete!");
    return 0xFFFF;
  }

  uint16_t value = Wire.read();
  value = (value << 8) | Wire.read();

  return value;
}

void setup() {
  Wire.begin();
  Wireling.begin();

  delay(500);

  Wireling.selectPort(SENSOR_PORT);

  SerialMonitorInterface.begin(115200);
  delay(1000);

  SerialMonitorInterface.println("\n=== TinyCircuits Sensor Test ===");
}

void loop() {
  Wireling.selectPort(SENSOR_PORT);

  SerialMonitorInterface.println("\n--------------------");

  bool ack = checkI2C();

  uint16_t moisture = readRegister(1);
  uint16_t temperature = readRegister(2);

  SerialMonitorInterface.print("Moisture RAW: 0x");
  SerialMonitorInterface.println(moisture, HEX);

  SerialMonitorInterface.print("Temperature RAW: 0x");
  SerialMonitorInterface.println(temperature, HEX);

  SerialMonitorInterface.print("Moisture decimal: ");
  SerialMonitorInterface.println(moisture);

  SerialMonitorInterface.print("Temperature decimal: ");
  SerialMonitorInterface.println(temperature);

  if (!ack) {
    SerialMonitorInterface.println(">>> SENSOR NOT ACKNOWLEDGING");
  }
  else if (moisture == 0xFFFF &&
           temperature == 0xFFFF) {
    SerialMonitorInterface.println(">>> INVALID DATA / POSSIBLE SENSOR FAILURE");
  }
  else {
    SerialMonitorInterface.println(">>> SENSOR RESPONDING");
  }

  delay(1000);
}

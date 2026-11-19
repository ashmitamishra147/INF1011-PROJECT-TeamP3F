/*
 * MoistureSetAddress.ino  -- RUN ONCE
 *
 * Changes the I2C address of the TinyCircuits Soil Moisture Wireling (AST1037,
 * a Chirp-style sensor) from its factory default 0x20 to a new address.
 *
 * WHY THIS IS NEEDED
 * ------------------
 * The TinyScreen+ board itself uses I2C address 0x20 (its GPIO/button/display
 * controller), and the moisture sensor also ships at 0x20. Those two collide
 * on the shared bus, so the sensor can never be read reliably while stacked on
 * a TinyScreen+. The mux on the Wireling adapter does NOT help here because the
 * TinyScreen's 0x20 is on the main bus, not behind the mux.
 *
 * Moving the sensor to 0x22 removes the collision. (0x21 is the TinyScreen's
 * alternate address, so we skip it.)
 *
 * HOW TO USE
 * ----------
 *   1. Make sure ONLY the moisture sensor is on the adapter, on MOISTURE_PORT.
 *   2. Set NEW_ADDRESS below if you want something other than 0x22.
 *   3. Upload this sketch, open Serial monitor at 115200.
 *   4. It writes the new address, resets the sensor, then scans to confirm.
 *   5. Once it reports success, upload your normal sketch again. The address
 *      change is PERSISTENT (stored in the sensor), so you only do this once.
 *
 * The Chirp protocol: write the new address (1 byte) to register 0x01, then
 * write 0x06 (reset) for it to take effect. Newer firmware requires the
 * address write to be sent twice as a safety measure, so we do that.
 *
 * Libraries: Wireling (TinyCircuits).
 */

#include <Wire.h>
#include <Wireling.h>

#define MOISTURE_PORT 1          // the port the moisture sensor is plugged into
const uint8_t OLD_ADDRESS = 0x20;   // factory default
const uint8_t NEW_ADDRESS = 0x22;   // desired new address

#define REG_SET_ADDRESS 0x01
#define REG_RESET       0x06

void writeReg8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void writeCmd(uint8_t addr, uint8_t cmd) {
  Wire.beginTransmission(addr);
  Wire.write(cmd);
  Wire.endTransmission();
}

bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  Wire.begin();
  Wireling.begin();
  Wireling.enablePower();
  delay(50);
  Wireling.selectPort(MOISTURE_PORT);
  delay(50);

  Serial.println(F("=== Moisture sensor address changer ==="));
  Serial.print(F("Target: move sensor from 0x"));
  Serial.print(OLD_ADDRESS, HEX);
  Serial.print(F(" to 0x"));
  Serial.println(NEW_ADDRESS, HEX);
  Serial.println();

  // If it already answers at the new address, we're done.
  if (present(NEW_ADDRESS)) {
    Serial.print(F("Sensor already present at 0x"));
    Serial.print(NEW_ADDRESS, HEX);
    Serial.println(F(" -- nothing to do. Upload your normal sketch."));
    return;
  }

  Serial.println(F("Writing new address to register 0x01 (twice), then reset..."));
  writeReg8(OLD_ADDRESS, REG_SET_ADDRESS, NEW_ADDRESS);
  delay(10);
  writeReg8(OLD_ADDRESS, REG_SET_ADDRESS, NEW_ADDRESS);  // required by newer FW
  delay(10);
  writeCmd(OLD_ADDRESS, REG_RESET);
  delay(1500);   // let the sensor reboot with the new address

  // Confirm.
  if (present(NEW_ADDRESS)) {
    Serial.print(F("SUCCESS: sensor now responds at 0x"));
    Serial.println(NEW_ADDRESS, HEX);
    Serial.println(F("Set MOISTURE_ADDR to this value in your sketches."));
  } else {
    Serial.println(F("Could not confirm the new address. Things to check:"));
    Serial.println(F("  - Is ONLY the moisture sensor on MOISTURE_PORT?"));
    Serial.println(F("  - Note: 0x20 is shared with the TinyScreen, so reading"));
    Serial.println(F("    0x20 does not prove the sensor is there."));
    Serial.println(F("  - Try power-cycling and running this sketch again."));
  }
}

void loop() {
  // nothing; this is a one-shot utility
}

/*
 * Sensors.h
 *
 * Thin drivers for the three measurement Wirelings used in the plant monitor,
 * all reached through the Wireling Adapter TinyShield (ASD2022). The adapter is
 * a 4-port I2C multiplexer, so every read must first select the port the
 * Wireling is plugged into with Wireling.selectPort(port).
 *
 * Wirelings and the chips they carry:
 *   - Temperature  (AST1021) -> NXP PCT2075   I2C 0x48  (11-bit temp register)
 *   - Ambient light(AST1012) -> AMS TSL2572    I2C 0x39  (ALS -> lux)
 *   - Soil moisture(AST1037) -> capacitive I2C sensor 0x20 (raw count -> %)
 *
 * The buzzer (AST1035) is a passive buzzer driven with tone() on an Arduino
 * pin, so it is handled directly in the .ino rather than here.
 *
 * NOTE ON PORTS: change MOISTURE_PORT / LIGHT_PORT / TEMP_PORT below to match
 * how you physically cabled the Wirelings to the adapter. Port numbers are
 * printed on the back of the adapter board (0-3).
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <Wireling.h>

// ---- Which adapter port each Wireling is plugged into (0-3) ----------------
#ifndef LIGHT_PORT
#define LIGHT_PORT 0
#endif
#ifndef MOISTURE_PORT
#define MOISTURE_PORT 1
#endif
#ifndef TEMP_PORT
#define TEMP_PORT 2
#endif
// Buzzer port is informational only; the buzzer is driven by a digital pin.
#ifndef BUZZER_PORT
#define BUZZER_PORT 3
#endif

// ---- I2C addresses ---------------------------------------------------------
static const uint8_t PCT2075_ADDR  = 0x48;  // temperature
static const uint8_t TSL2572_ADDR  = 0x39;  // ambient light
// NOTE: the moisture sensor ships at 0x20, which COLLIDES with the TinyScreen+
// (also 0x20 on the shared I2C bus). Run tests/MoistureSetAddress once to move
// the sensor to 0x22, which is what this driver expects.
static const uint8_t MOISTURE_ADDR = 0x22;  // capacitive soil moisture (relocated)

// ---- Moisture calibration (raw counts) -------------------------------------
// Higher raw count = wetter. These are rough starting points for the Chirp-
// style capacitive sensor; EVERY sensor reads a little differently, so check
// the raw value printed to Serial in dry air vs submerged in water and update
// these two numbers for accurate percentages.
static uint16_t moistureDry = 280;    // typical reading in dry air
static uint16_t moistureWet = 560;    // typical reading submerged in water

// ---------------------------------------------------------------------------
// Reading struct returned to the UI layer.
// ---------------------------------------------------------------------------
struct SensorReadings {
  float    tempC;        // degrees Celsius
  float    lux;          // ambient light in lux
  uint16_t moistureRaw;  // raw capacitive count
  int8_t   moisturePct;  // 0-100 %, clamped
  bool     tempOk;       // false if the sensor did not respond
  bool     lightOk;
  bool     moistureOk;
};

// ---------------------------------------------------------------------------
// Low-level I2C helpers
// ---------------------------------------------------------------------------
static bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool i2cReadN(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;   // repeated start
  uint8_t got = Wire.requestFrom(addr, n);
  if (got != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// ---------------------------------------------------------------------------
// TSL2572 ambient-light sensor
// ---------------------------------------------------------------------------
// Register map (command bit 0x80 | auto-increment 0x20):
//   0x00 ENABLE, 0x01 ATIME, 0x0D CONFIG(gain x1), 0x0F CONTROL(gain)
//   0x14..0x17 C0/C1 ADC data
#define TSL_CMD        0x80
#define TSL_AUTOINC    0x20

static float tsl2572GainVal = 8.0f;   // configured below (x8)
static float tsl2572AtimeMs = 51.0f;  // 256 - ATIME(0xED)=19 cycles -> ~51ms

static bool tsl2572Begin() {
  // Power on + enable ADC.
  if (!i2cWrite8(TSL2572_ADDR, TSL_CMD | 0x00, 0x03)) return false; // PON|AEN
  // ATIME: 0xED -> 19 integration cycles (~51 ms, max count ~65535).
  i2cWrite8(TSL2572_ADDR, TSL_CMD | 0x01, 0xED);
  // Gain x8 (CONTROL reg 0x0F, AGAIN bits = 0x02). Also clear the x0.16 scale.
  i2cWrite8(TSL2572_ADDR, TSL_CMD | 0x0F, 0x02);
  i2cWrite8(TSL2572_ADDR, TSL_CMD | 0x0D, 0x00);
  return true;
}

static bool tsl2572ReadLux(float& lux) {
  uint8_t d[4];
  if (!i2cReadN(TSL2572_ADDR, TSL_CMD | TSL_AUTOINC | 0x14, d, 4)) return false;
  uint16_t c0 = (uint16_t)d[0] | ((uint16_t)d[1] << 8);   // full spectrum
  uint16_t c1 = (uint16_t)d[2] | ((uint16_t)d[3] << 8);   // IR

  // Datasheet lux approximation for the TSL2572.
  float cpl = (tsl2572AtimeMs * tsl2572GainVal) / 60.0f;  // counts-per-lux
  if (cpl <= 0.0f) cpl = 1.0f;
  float lux1 = ((float)c0 - 1.87f * (float)c1) / cpl;
  float lux2 = (0.63f * (float)c0 - 1.0f * (float)c1) / cpl;
  lux = max(lux1, lux2);
  if (lux < 0) lux = 0;
  return true;
}

// ---------------------------------------------------------------------------
// PCT2075 temperature sensor
// ---------------------------------------------------------------------------
static bool pct2075ReadC(float& tempC) {
  uint8_t d[2];
  if (!i2cReadN(PCT2075_ADDR, 0x00, d, 2)) return false;  // temp register
  // 11-bit, left-justified in a 16-bit value; LSB = 0.125 C.
  int16_t raw = ((int16_t)d[0] << 8 | d[1]);
  raw >>= 5;                       // drop 5 unused low bits
  tempC = raw * 0.125f;
  return true;
}

// ---------------------------------------------------------------------------
// Capacitive soil-moisture sensor (Chirp / i2c-moisture-sensor)
// ---------------------------------------------------------------------------
// The TinyCircuits moisture Wireling is the Chirp/Catnip capacitive sensor
// (an ATTINY running custom firmware) at address 0x20.
//
// IMPORTANT: this chip does NOT support a combined write-then-read with a
// repeated start (endTransmission(false)). The required sequence is:
//   1. write the register byte, then send a STOP  (endTransmission(true))
//   2. wait ~20 ms for the sensor to prepare the value
//   3. requestFrom() and read the bytes
// Using a repeated start (as the generic i2cReadN helper does) makes the
// sensor NACK, which is why reads were failing with ERR regardless of
// moisture. That is why this sensor gets its own register helpers below.
//
// Register map:
//   0x00 GET_CAPACITANCE (2 bytes, big-endian) -> higher = wetter
//   0x05 GET_VERSION     (1 byte)  e.g. 0x22 = firmware 2.2
//   0x06 RESET           (write only)
#define MOIST_REG_CAPACITANCE 0x00
#define MOIST_REG_VERSION     0x05
#define MOIST_REG_RESET       0x06

// Write a single register byte with a proper STOP condition.
static void moistureWriteReg(uint8_t reg) {
  Wire.beginTransmission(MOISTURE_ADDR);
  Wire.write(reg);
  Wire.endTransmission(true);          // STOP (not a repeated start)
}

// Read a 16-bit big-endian register using the Chirp sequence.
static bool moistureRead16(uint8_t reg, uint16_t& out) {
  moistureWriteReg(reg);
  delay(20);                           // sensor needs time to latch the value
  uint8_t got = Wire.requestFrom((uint8_t)MOISTURE_ADDR, (uint8_t)2);
  if (got != 2) return false;
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  out = ((uint16_t)hi << 8) | lo;      // big-endian
  return true;
}

// Read an 8-bit register (e.g. firmware version) using the Chirp sequence.
static bool moistureRead8(uint8_t reg, uint8_t& out) {
  moistureWriteReg(reg);
  delay(20);
  uint8_t got = Wire.requestFrom((uint8_t)MOISTURE_ADDR, (uint8_t)1);
  if (got != 1) return false;
  out = Wire.read();
  return true;
}

static bool moistureReadRaw(uint16_t& raw) {
  // A fresh-booted Chirp sensor reports ~290-320 in free air at 5V. A raw
  // value of 0 or 0xFFFF usually means the read did not really succeed, so
  // treat those as a failure too.
  if (!moistureRead16(MOIST_REG_CAPACITANCE, raw)) return false;
  if (raw == 0 || raw == 0xFFFF) return false;
  return true;
}

static int8_t moisturePercent(uint16_t raw) {
  if (moistureWet <= moistureDry) return 0;
  long pct = (long)(raw - moistureDry) * 100 / (moistureWet - moistureDry);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return (int8_t)pct;
}

// ---------------------------------------------------------------------------
// I2C scanner (debug helper)
// ---------------------------------------------------------------------------
// Selects the given Wireling adapter port, then probes every 7-bit I2C
// address (0x08..0x77) and prints the ones that ACK to Serial. Handy for
// confirming the moisture sensor is present and at which address -- a Chirp
// sensor normally answers at 0x20 but can shift (e.g. to 0x45) if hot-plugged.
inline void scanI2CPort(uint8_t port) {
  Wireling.selectPort(port);
  Serial.print(F("[i2c scan] port "));
  Serial.print(port);
  Serial.println(F(":"));

  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {   // 0 == device ACKed
      Serial.print(F("  device at 0x"));
      if (addr < 0x10) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("  no devices found (check cable/port/power)"));
  } else {
    Serial.print(F("  total: "));
    Serial.println(found);
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Initialise the Wireling mux and each sensor. Call once in setup().
inline void sensorsBegin() {
  Wireling.begin();

  Wireling.selectPort(LIGHT_PORT);
  tsl2572Begin();

  // Chirp moisture sensor: issue a RESET and let it boot (~0.5-1 s). The
  // PCT2075 temperature sensor needs no configuration writes.
  Wireling.selectPort(MOISTURE_PORT);
  moistureWriteReg(MOIST_REG_RESET);
  delay(1000);

  // One-time sanity check: read the firmware version. If this comes back
  // (e.g. 0x22 = v2.2) the sensor is talking; if not, it is a wiring / port /
  // hardware problem rather than software. Printed to Serial for debugging.
  uint8_t ver = 0;
  bool verOk = moistureRead8(MOIST_REG_VERSION, ver);
  Serial.print(F("[moisture] port "));
  Serial.print(MOISTURE_PORT);
  Serial.print(F(" firmware version: "));
  if (verOk) {
    Serial.print(ver >> 4); Serial.print('.'); Serial.println(ver & 0x0F);
  } else {
    Serial.println(F("NO RESPONSE (check cable/port/address)"));
  }
}

// Take one fresh set of readings from all three sensors.
inline SensorReadings sensorsRead() {
  SensorReadings r;
  r.tempC = r.lux = 0;
  r.moistureRaw = 0;
  r.moisturePct = 0;
  r.tempOk = r.lightOk = r.moistureOk = false;

  // --- Temperature ---
  Wireling.selectPort(TEMP_PORT);
  r.tempOk = pct2075ReadC(r.tempC);

  // --- Light ---
  Wireling.selectPort(LIGHT_PORT);
  r.lightOk = tsl2572ReadLux(r.lux);

  // --- Moisture ---
  Wireling.selectPort(MOISTURE_PORT);
  r.moistureOk = moistureReadRaw(r.moistureRaw);
  if (r.moistureOk) r.moisturePct = moisturePercent(r.moistureRaw);

  return r;
}

#endif  // SENSORS_H

/*
 * PlantDisplay.ino
 *
 * LIVE plant-monitor on a TinyScreen+ (96x64 OLED) driven by real Wireling
 * sensors, read through the Wireling Adapter TinyShield (ASD2022).
 *
 * Hardware stack (bottom to top):
 *   TinyScreen+  ->  Wireling Adapter TinyShield (ASD2022)
 *
 * Wirelings plugged into the adapter ports (edit the *_PORT defines below to
 * match your cabling; port numbers are printed on the back of the adapter):
 *   Port 0 : Ambient Light Sensor   (AST1012, TSL2572)
 *   Port 1 : Soil Moisture Sensor   (AST1037)
 *   Port 2 : Temperature Sensor     (AST1021, PCT2075)
 *   Port 3 : Buzzer                 (AST1035)   <- driven by a digital pin
 *
 * What it does:
 *   - Reads temperature (C), ambient light (lux) and soil moisture (%) once
 *     per second.
 *   - Shows a numeric readout screen with all three values, plus live glyphs
 *     (sun/moon, thermometer, water drop) that reflect the real readings.
 *   - Beeps the buzzer briefly when soil moisture drops below a dry threshold.
 *   - Also prints every reading to Serial (115200 baud) so you can watch the
 *     raw numbers in the Arduino IDE serial monitor while testing.
 *
 * Libraries (install via Arduino Library Manager):
 *   - TinyScreen   (search "TinyScreen")
 *   - Wireling     (search "Wireling")
 */

#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>

// ---- Pick the adapter port for each Wireling BEFORE including Sensors.h ----
#define LIGHT_PORT    0
#define MOISTURE_PORT 1
#define TEMP_PORT     2
#define BUZZER_PORT   3

#include "PlantTypes.h"   // Light / Temp / Soil enums
#include "Sensors.h"      // Wireling sensor drivers (uses the *_PORT defines)

// Use TinyScreenPlus for the TinyScreen+ board.
TinyScreen display = TinyScreen(TinyScreenPlus);

const uint8_t SCREEN_W = 96;
const uint8_t SCREEN_H = 64;

// How often to sample the sensors + refresh the screen.
const unsigned long SAMPLE_MS = 1000;

// ---- Buzzer ----------------------------------------------------------------
// The AST1035 buzzer Wireling is driven from the adapter port's digital/analog
// line. On a TinyScreen+ the four Wireling ports map to pins A0..A3; port 3's
// signal line is pin A3. Adjust if you move the buzzer to another port.
const uint8_t BUZZER_PIN   = A3;
const int     BUZZER_FREQ  = 2000;   // Hz
const int     DRY_ALERT_PCT = 25;    // beep when moisture drops below this %

// ---- Classification thresholds (tune to taste) -----------------------------
// Temperature bands (Celsius).
const float TEMP_HOT_C  = 28.0f;
const float TEMP_COLD_C = 18.0f;
// Light bands (lux).
const float LUX_BRIGHT  = 500.0f;
const float LUX_DARK    = 50.0f;
// Soil moisture bands (%).
const int   SOIL_WET_PCT = 70;
const int   SOIL_DRY_PCT = 30;

// --- 8-bit (3-3-2) color helper ---------------------------------------------
uint8_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint8_t)(((r & 0x07) << 5) | ((g & 0x07) << 2) | (b & 0x03));
}

// Palette (built in setup).
const uint8_t COL_SKY = 0x00;
uint8_t colSoilC, colPot, colPotRim, colStem, colLeaf, colLeafDk;
uint8_t colGround, colText, colLabel, colFrame, colBad;
uint8_t colSun, colSunHot, colMoon, colStar;
uint8_t colTempHot, colTempMid, colTempCold, colTempTube;
uint8_t colDrop, colDropWarn;

unsigned long lastSample = 0;
bool buzzerActive = false;
unsigned long buzzerOffAt = 0;
uint8_t lastButtons = 0;         // previous button bitmask, for edge detection

// Start a non-blocking buzzer chirp of the given length (ms).
void startBeep(unsigned long durationMs) {
  tone(BUZZER_PIN, BUZZER_FREQ);
  buzzerActive = true;
  buzzerOffAt = millis() + durationMs;
}

// Forward declarations.
void drawLive(const SensorReadings& r);
void logReadings(const SensorReadings& r);
Light classifyLight(float lux);
Temp  classifyTemp(float c);
Soil  classifySoil(int pct);

void setup() {
  Serial.begin(115200);
  Wire.begin();

  display.begin();
  display.setBrightness(12);     // 0-15
  display.setFlip(false);

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // Bring up the Wireling mux + sensors.
  sensorsBegin();

  // One-time I2C scan of the moisture port, printed to Serial. Tells you
  // whether the sensor responds and at which address (expect 0x20).
  scanI2CPort(MOISTURE_PORT);

  // Build the palette.
  colSoilC    = rgb(3, 1, 0);
  colPot      = rgb(6, 2, 0);
  colPotRim   = rgb(7, 3, 1);
  colStem     = rgb(1, 4, 0);
  colLeaf     = rgb(1, 6, 0);
  colLeafDk   = rgb(0, 4, 0);
  colGround   = rgb(2, 2, 1);
  colText     = rgb(7, 7, 3);    // bright value text
  colLabel    = rgb(4, 7, 7);    // cyan label text
  colFrame    = rgb(1, 3, 3);
  colBad      = rgb(7, 0, 0);    // sensor error
  colSun      = rgb(7, 5, 0);
  colSunHot   = rgb(7, 2, 0);
  colMoon     = rgb(4, 4, 2);
  colStar     = rgb(6, 6, 3);
  colTempHot  = rgb(7, 0, 0);
  colTempMid  = rgb(7, 3, 0);
  colTempCold = rgb(1, 4, 3);
  colTempTube = rgb(3, 3, 3);
  colDrop     = rgb(1, 4, 3);
  colDropWarn = rgb(7, 4, 0);

  lastSample = millis() - SAMPLE_MS;   // force an immediate first read
}

void loop() {
  unsigned long now = millis();

  // --- Buzzer test: chirp once each time a TinyScreen button is pressed ---
  // getButtons() returns a bitmask of the 4 on-board buttons. Edge detection
  // (newly-pressed = bits set now but not last loop) gives one beep per press
  // instead of a continuous tone while a button is held.
  uint8_t buttons = display.getButtons();
  uint8_t justPressed = buttons & ~lastButtons;
  if (justPressed) {
    Serial.print("Button press: 0x");
    Serial.println(justPressed, HEX);
    startBeep(120);                    // 120 ms test chirp
  }
  lastButtons = buttons;

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;

    SensorReadings r = sensorsRead();
    logReadings(r);
    drawLive(r);

    // Buzzer: short beep when soil is too dry (and the sensor responded).
    // Skip if a button chirp is already sounding so they don't stomp on
    // each other.
    if (!buzzerActive && r.moistureOk && r.moisturePct < DRY_ALERT_PCT) {
      startBeep(150);                  // 150 ms dry-soil chirp
    }
  }

  // Non-blocking buzzer off.
  if (buzzerActive && now >= buzzerOffAt) {
    noTone(BUZZER_PIN);
    buzzerActive = false;
  }
}

// ---------------------------------------------------------------------------
// Serial logging (handy while testing)
// ---------------------------------------------------------------------------
void logReadings(const SensorReadings& r) {
  Serial.print("Temp: ");
  if (r.tempOk) { Serial.print(r.tempC, 1); Serial.print(" C"); }
  else            Serial.print("--");
  Serial.print(" | Light: ");
  if (r.lightOk) { Serial.print(r.lux, 0); Serial.print(" lux"); }
  else             Serial.print("--");
  Serial.print(" | Moisture: ");
  if (r.moistureOk) {
    Serial.print(r.moisturePct); Serial.print("% (raw ");
    Serial.print(r.moistureRaw); Serial.print(")");
  } else Serial.print("--");
  Serial.println();
}

// ---------------------------------------------------------------------------
// Classification of raw readings into the display bands
// ---------------------------------------------------------------------------
Light classifyLight(float lux) {
  if (lux >= LUX_BRIGHT) return LIGHT_HOT;       // bright
  if (lux <= LUX_DARK)   return LIGHT_COLD_DARK; // dark
  return LIGHT_NORMAL;
}
Temp classifyTemp(float c) {
  if (c >= TEMP_HOT_C)  return TEMP_HOT;
  if (c <= TEMP_COLD_C) return TEMP_COLD;
  return TEMP_NORMAL;
}
Soil classifySoil(int pct) {
  if (pct >= SOIL_WET_PCT) return SOIL_OVERWET;
  if (pct <= SOIL_DRY_PCT) return SOIL_DRY;
  return SOIL_JUST;
}

// ---------------------------------------------------------------------------
// Main live screen: numeric readout + live glyphs
// ---------------------------------------------------------------------------
void drawLive(const SensorReadings& r) {
  Light lgt  = classifyLight(r.lux);
  Temp  tmp  = classifyTemp(r.tempC);
  Soil  soil = classifySoil(r.moisturePct);

  // Background + frame.
  display.drawRect(0, 0, SCREEN_W, SCREEN_H, TSRectangleFilled, COL_SKY);
  drawRectOutline(0, 0, SCREEN_W, SCREEN_H, colFrame);

  display.setFont(thinPixel7_10ptFontInfo);

  // Title.
  display.fontColor(colLabel, COL_SKY);
  display.setCursor(4, 2);
  display.print("PLANT MONITOR");

  // --- Temperature line ---
  display.fontColor(colLabel, COL_SKY);
  display.setCursor(4, 16);
  display.print("Temp:");
  display.fontColor(r.tempOk ? colText : colBad, COL_SKY);
  display.setCursor(44, 16);
  if (r.tempOk) {
    display.print(r.tempC, 1);   // one decimal place
    display.print(" C");
  } else {
    display.print("ERR");
  }

  // --- Light line ---
  display.fontColor(colLabel, COL_SKY);
  display.setCursor(4, 28);
  display.print("Light:");
  display.fontColor(r.lightOk ? colText : colBad, COL_SKY);
  display.setCursor(44, 28);
  if (r.lightOk) {
    display.print((long)(r.lux + 0.5f));   // rounded whole lux
    display.print(" lx");
  } else {
    display.print("ERR");
  }

  // --- Moisture line ---
  display.fontColor(colLabel, COL_SKY);
  display.setCursor(4, 40);
  display.print("Soil:");
  display.fontColor(r.moistureOk ? colText : colBad, COL_SKY);
  display.setCursor(44, 40);
  if (r.moistureOk) {
    display.print((int)r.moisturePct);
    display.print(" %");
  } else {
    display.print("ERR");
  }

  // --- Status label (bottom) ---
  const char* status = "SOIL OK";
  uint8_t statusCol = colLeaf;
  if (soil == SOIL_DRY)      { status = "NEEDS WATER"; statusCol = colDropWarn; }
  else if (soil == SOIL_OVERWET) { status = "OVERWET"; statusCol = colDrop; }
  display.fontColor(statusCol, COL_SKY);
  display.setCursor(4, 53);
  display.print(status);

  // --- Live glyphs on the right ---
  if (lgt == LIGHT_COLD_DARK) drawMoon(80, 18);
  else drawSun(80, 18, (lgt == LIGHT_HOT) ? colSunHot : colSun);

  drawThermometer(80, 30, tmp);
  drawMoisture(80, 52, soil);
}

// ---------------------------------------------------------------------------
// Sky objects
// ---------------------------------------------------------------------------
void drawSun(uint8_t cx, uint8_t cy, uint8_t col) {
  for (uint8_t a = 0; a < 8; a++) {
    int dx = 0, dy = 0;
    switch (a) {
      case 0: dx =  0; dy = -1; break;
      case 1: dx =  1; dy = -1; break;
      case 2: dx =  1; dy =  0; break;
      case 3: dx =  1; dy =  1; break;
      case 4: dx =  0; dy =  1; break;
      case 5: dx = -1; dy =  1; break;
      case 6: dx = -1; dy =  0; break;
      case 7: dx = -1; dy = -1; break;
    }
    drawLine(cx + dx * 6, cy + dy * 6, cx + dx * 9, cy + dy * 9, col);
  }
  fillCircle(cx, cy, 4, col);
}

void drawMoon(uint8_t cx, uint8_t cy) {
  fillCircle(cx, cy, 5, colMoon);
  fillCircle(cx + 3, cy - 2, 5, COL_SKY);
  plot(cx + 7, cy - 6, colStar);
  plot(cx - 7, cy + 5, colStar);
}

// ---------------------------------------------------------------------------
// Thermometer
// ---------------------------------------------------------------------------
void drawThermometer(uint8_t cx, uint8_t topY, Temp t) {
  const uint8_t tubeH = 10;
  const uint8_t bulbR = 3;
  uint8_t bulbY = topY + tubeH + 2;

  drawRectOutline(cx - 1, topY, 3, tubeH, colTempTube);
  fillCircle(cx, bulbY, bulbR, colTempTube);

  uint8_t fillCol;
  uint8_t fillTop;
  switch (t) {
    case TEMP_HOT:    fillCol = colTempHot;  fillTop = topY + 2;  break;
    case TEMP_NORMAL: fillCol = colTempMid;  fillTop = topY + 5;  break;
    default:          fillCol = colTempCold; fillTop = topY + 8;  break;
  }
  display.drawRect(cx, fillTop, 1, (bulbY - fillTop), TSRectangleFilled, fillCol);
  fillCircle(cx, bulbY, bulbR - 1, fillCol);
}

// ---------------------------------------------------------------------------
// Moisture indicator
// ---------------------------------------------------------------------------
void drawMoisture(uint8_t cx, uint8_t cy, Soil soil) {
  switch (soil) {
    case SOIL_OVERWET:
      drawDrop(cx, cy, colDrop, 3);
      plot(cx - 5, cy + 1, colDrop);
      plot(cx + 5, cy + 1, colDrop);
      break;
    case SOIL_JUST:
      drawDrop(cx, cy, colDrop, 3);
      break;
    case SOIL_DRY:
      drawDrop(cx, cy, colDropWarn, 3);
      display.drawRect(cx + 6, cy - 3, 1, 4, TSRectangleFilled, colDropWarn);
      plot(cx + 6, cy + 2, colDropWarn);
      break;
  }
}

void drawDrop(uint8_t cx, uint8_t cy, uint8_t col, uint8_t r) {
  fillCircle(cx, cy, r, col);
  for (uint8_t i = 0; i <= r; i++) {
    uint8_t w = (r - i);
    display.drawRect(cx - w, cy - r - i, (w * 2 + 1), 1, TSRectangleFilled, col);
  }
}

// ---------------------------------------------------------------------------
// Small drawing helpers
// ---------------------------------------------------------------------------
void drawRectOutline(int x, int y, int w, int h, uint8_t col) {
  display.drawRect(x, y, w, 1, TSRectangleFilled, col);
  display.drawRect(x, y + h - 1, w, 1, TSRectangleFilled, col);
  display.drawRect(x, y, 1, h, TSRectangleFilled, col);
  display.drawRect(x + w - 1, y, 1, h, TSRectangleFilled, col);
}

void drawLine(int x0, int y0, int x1, int y1, uint8_t col) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    plot(x0, y0, col);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void fillCircle(int cx, int cy, int r, uint8_t col) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) plot(cx + x, cy + y, col);
    }
  }
}

void plot(int x, int y, uint8_t col) {
  if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
  display.drawRect(x, y, 1, 1, TSRectangleFilled, col);
}

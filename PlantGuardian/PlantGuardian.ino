/*
  Plant Vitals Guardian - Tamagotchi-style plant pet for TinyScreen+ (96x64 OLED)

  SIMULATION BUILD: sensor readings come from a hardcoded 48-hour sample table
  (see TIMELINE below). When the Wireling sensors are ready, set
  USE_SIMULATED_DATA to 0 and fill in the three read*() functions.

  Soil moisture has two limits: below 30% the plant is THIRSTY, above 80% it is
  SOGGY (over-watered). Healthy range is 30-80%.

  Screens (cycle with the bottom buttons):
    1. PET     - the plant's face, driven by its mood
    2. VITALS  - the raw numbers behind the mood, with threshold markers
    3. SOIL    - soil moisture graph + predicted next watering time
    4. TEMP    - temperature graph
    5. LIGHT   - light graph (log scale)
  (dashed lines on each graph mark the mood thresholds; the soil graph has two)

  Buttons:
    Lower-right : next screen
    Lower-left  : previous screen
    Upper-left  : simulation mode (48h timeline -> force Happy/Thirsty/Cold/Sleepy/Soggy)
    Upper-right : pause / resume the timeline
*/

#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>

// ---------------------------------------------------------------- config
// 1 = built-in sample timeline, 0 = read the real Wirelings (light, soil moisture, temperature)
#define USE_SIMULATED_DATA 0

// Wireling adapter ports (0-3, printed on the adapter). Must be defined before Sensors.h.
#define LIGHT_PORT    0
#define MOISTURE_PORT 3   // adapter port 1 on this unit pulls the I2C data line low, so soil moisture lives on port 3
#define TEMP_PORT     2
#define BUZZER_PORT   1
#if !USE_SIMULATED_DATA
#include "Sensors.h"   // Wireling drivers: PCT2075 (temp), TSL2572 (light), capacitive soil moisture
#endif

// 1 = also send every reading over the WiFi TinyShield to the dashboard server (so it works on battery, no USB).
// Fill in secrets.h first. USB serial output keeps working either way.
#define USE_WIFI 1
#if USE_WIFI && !USE_SIMULATED_DATA
#include <WiFi101.h>
#include "secrets.h"
#endif

const unsigned long SENSOR_INTERVAL_MS = 2000;   // "check every 2 seconds"
const unsigned long ANIM_INTERVAL_MS   = 350;    // face animation speed
const unsigned long DIM_AFTER_MS       = 30000;  // dim if nothing changes for 30 s
const uint8_t BRIGHT_NORMAL = 10;                // 0-15
const uint8_t BRIGHT_DIM    = 3;

// Mood thresholds
// Mood limits. These are the defaults; with WiFi on, the dashboard's chosen plant overrides them (see applyLimits).
float MOISTURE_THIRSTY = 30.0;  // % below this -> Thirsty
float MOISTURE_SOGGY   = 80.0;  // % above this -> Soggy (over-watered)
float TEMP_COLD        = 18.0;  // C below this -> Cold
int   LIGHT_DARK       = 100;   // lux below this -> Sleepy

// One simulated sample = 30 minutes of plant time (used for watering prediction)
const float HOURS_PER_SAMPLE = 0.5;

// ---------------------------------------------------------------- colors
// TinyScreen 8-bit color format is BBBGGGRR
#define RGB8(r, g, b) ((uint8_t)((((b) & 7) << 5) | (((g) & 7) << 2) | ((r) & 3)))

const uint8_t C_BG       = TS_8b_Black;
const uint8_t C_FACE     = TS_8b_Black;
const uint8_t C_TEXT     = TS_8b_White;
const uint8_t C_MUTED    = TS_8b_Gray;
const uint8_t C_TRACK    = RGB8(1, 1, 1);
const uint8_t C_OK       = TS_8b_Green;
const uint8_t C_BAD      = TS_8b_Red;
const uint8_t C_POT      = TS_8b_Brown;
const uint8_t C_POT_RIM  = TS_8b_DarkBrown;
const uint8_t C_LEAF     = TS_8b_DarkGreen;
const uint8_t C_LEAF_ROT = RGB8(1, 3, 0);   // olive: leaves of an over-watered plant
const uint8_t C_CHEEK    = RGB8(3, 2, 5);
const uint8_t C_WATER    = RGB8(1, 6, 7);

// ---------------------------------------------------------------- types
enum Mood { MOOD_HAPPY, MOOD_THIRSTY, MOOD_COLD, MOOD_SLEEPY, MOOD_SOGGY, MOOD_COUNT };
enum View {
  VIEW_PET, VIEW_VITALS, VIEW_GRAPH_SOIL, VIEW_GRAPH_TEMP, VIEW_GRAPH_LIGHT, VIEW_COUNT };
enum Metric { MET_SOIL, MET_TEMP, MET_LIGHT, MET_COUNT };

struct Sample {
  uint16_t minuteOfDay;  // simulated clock
  uint8_t  moisture;     // %
  float    temp;         // C
  uint16_t lux;
};

struct Preset {
  float moisture;
  float temp;
  int   lux;
};

const char *MOOD_NAMES[MOOD_COUNT] = { "HAPPY", "THIRSTY", "COLD", "SLEEPY", "SOGGY" };

// ---------------------------------------------------------------- sample data
// Two simulated days starting 06:00, one row per 30 min (96 rows, then it loops):
//   Day 1: 06:00-16:00 happy, 16:30-20:30 thirsty (soil dries out),
//          21:00 watered properly (21% -> 62%) -> sleepy (dark), 00:30-05:30 cold
//   Day 2: soil dries out again (thirsty from 20:00), then 21:00 OVER-watered
//          (28% -> 94%) -> soggy, and the soil stays too wet through the night
const Sample TIMELINE[] = {
  {  360, 50, 18.3f,  120 }, {  390, 49, 18.8f,  219 }, {  420, 48, 19.2f,  316 }, {  450, 47, 19.8f,  411 },
  {  480, 46, 20.3f,  502 }, {  510, 45, 20.9f,  588 }, {  540, 44, 21.5f,  669 }, {  570, 43, 22.1f,  742 },
  {  600, 42, 22.7f,  808 }, {  630, 41, 23.2f,  865 }, {  660, 40, 23.8f,  913 }, {  690, 39, 24.2f,  951 },
  {  720, 38, 24.7f,  978 }, {  750, 37, 25.1f,  994 }, {  780, 36, 25.4f, 1000 }, {  810, 35, 25.7f,  994 },
  {  840, 34, 25.8f,  978 }, {  870, 33, 26.0f,  951 }, {  900, 32, 26.0f,  913 }, {  930, 31, 26.0f,  865 },
  {  960, 30, 25.8f,  808 }, {  990, 29, 25.7f,  742 }, { 1020, 28, 25.4f,  669 }, { 1050, 27, 25.1f,  588 },
  { 1080, 26, 24.7f,  502 }, { 1110, 25, 24.2f,  411 }, { 1140, 24, 23.8f,  316 }, { 1170, 23, 23.2f,  219 },
  { 1200, 22, 22.7f,    1 }, { 1230, 21, 22.1f,    2 }, { 1260, 62, 21.5f,    4 }, { 1290, 62, 20.9f,    7 },
  { 1320, 61, 20.3f,    7 }, { 1350, 61, 19.8f,    4 }, { 1380, 61, 19.2f,    1 }, { 1410, 60, 18.8f,    1 },
  {    0, 60, 18.3f,    4 }, {   30, 60, 17.9f,    6 }, {   60, 59, 17.6f,    7 }, {   90, 59, 17.3f,    5 },
  {  120, 59, 17.2f,    2 }, {  150, 58, 17.0f,    1 }, {  180, 58, 17.0f,    3 }, {  210, 57, 17.0f,    6 },
  {  240, 57, 17.2f,    7 }, {  270, 57, 17.3f,    6 }, {  300, 56, 17.6f,    3 }, {  330, 56, 17.9f,    1 },
  {  360, 56, 18.3f,  120 }, {  390, 55, 18.8f,  219 }, {  420, 54, 19.2f,  316 }, {  450, 53, 19.8f,  411 },
  {  480, 52, 20.3f,  502 }, {  510, 51, 20.9f,  588 }, {  540, 50, 21.5f,  669 }, {  570, 49, 22.1f,  742 },
  {  600, 48, 22.7f,  808 }, {  630, 47, 23.2f,  865 }, {  660, 47, 23.8f,  913 }, {  690, 46, 24.2f,  951 },
  {  720, 45, 24.7f,  978 }, {  750, 44, 25.1f,  994 }, {  780, 43, 25.4f, 1000 }, {  810, 42, 25.7f,  994 },
  {  840, 41, 25.8f,  978 }, {  870, 40, 26.0f,  951 }, {  900, 39, 26.0f,  913 }, {  930, 38, 26.0f,  865 },
  {  960, 37, 25.8f,  808 }, {  990, 36, 25.7f,  742 }, { 1020, 35, 25.4f,  669 }, { 1050, 34, 25.1f,  588 },
  { 1080, 33, 24.7f,  502 }, { 1110, 32, 24.2f,  411 }, { 1140, 31, 23.8f,  316 }, { 1170, 30, 23.2f,  219 },
  { 1200, 29, 22.7f,    1 }, { 1230, 28, 22.1f,    2 }, { 1260, 94, 21.5f,    4 }, { 1290, 94, 20.9f,    7 },
  { 1320, 93, 20.3f,    7 }, { 1350, 93, 19.8f,    4 }, { 1380, 92, 19.2f,    1 }, { 1410, 92, 18.8f,    1 },
  {    0, 92, 18.3f,    4 }, {   30, 91, 17.9f,    6 }, {   60, 91, 17.6f,    7 }, {   90, 90, 17.3f,    5 },
  {  120, 90, 17.2f,    2 }, {  150, 90, 17.0f,    1 }, {  180, 89, 17.0f,    3 }, {  210, 89, 17.0f,    6 },
  {  240, 88, 17.2f,    7 }, {  270, 88, 17.3f,    6 }, {  300, 88, 17.6f,    3 }, {  330, 87, 17.9f,    1 },
};
const uint8_t TIMELINE_LEN = sizeof(TIMELINE) / sizeof(TIMELINE[0]);

// Fixed readings used when a mood is forced with the upper-left button
const Preset PRESETS[MOOD_COUNT] = {
  { 55, 23.0, 600 },  // happy
  { 18, 24.0, 700 },  // thirsty
  { 60, 14.5, 400 },  // cold
  { 60, 21.0,   3 },  // sleepy
  { 92, 22.0, 600 },  // soggy (over-watered)
};

// ---------------------------------------------------------------- state
TinyScreen display = TinyScreen(TinyScreenPlus);

float moisture = 0, temperature = 0;
int   lightLevel = 0;
Mood  mood = MOOD_HAPPY;
View  view = VIEW_PET;

uint8_t simIndex = 0;     // position in TIMELINE
uint8_t simMode  = 0;     // 0 = timeline, 1..5 = force mood (simMode-1)
bool    paused   = false;

const uint8_t HISTORY_LEN = 40;
float   histSoil[HISTORY_LEN], histTemp[HISTORY_LEN], histLight[HISTORY_LEN];
uint8_t historyCount = 0;

float *historyFor(uint8_t metric) {
  return metric == MET_SOIL ? histSoil : metric == MET_TEMP ? histTemp : histLight;
}

unsigned long lastSensorMs = 0, lastAnimMs = 0, lastActivityMs = 0;
uint8_t animFrame = 0;
uint8_t lastButtons = 0;
bool    dimmed = false;
bool    blinking = false;

// ================================================================ sensors
// Each read*() returns calibrated units: moisture %, temperature C, light lux.

uint16_t simulatedClock() { return TIMELINE[simIndex].minuteOfDay; }

#if !USE_SIMULATED_DATA
// Latest good reading from each Wireling. If a sensor misses a read, its last good value is kept
// (so one hiccup doesn't put a 0 on the screen or the dashboard) and its OK flag goes false.
bool  moistureOk = false, tempOk = false, lightOk = false;
float lastMoisture = 50, lastTemperature = 22;
int   lastLight = 100;

void refreshSensors() {
  SensorReadings s = sensorsRead();
  moistureOk = s.moistureOk;  tempOk = s.tempOk;  lightOk = s.lightOk;
  if (moistureOk) lastMoisture    = s.moisturePct;
  if (tempOk)     lastTemperature = s.tempC;
  if (lightOk)    lastLight       = (int)(s.lux + 0.5f);
}
#endif

float readSoilMoisture() {
  if (simMode) return PRESETS[simMode - 1].moisture;      // demo preset (upper-left button)
#if USE_SIMULATED_DATA
  return TIMELINE[simIndex].moisture;
#else
  return lastMoisture;                                    // soil moisture Wireling, 0-100 %
#endif
}

float readTemperature() {
  if (simMode) return PRESETS[simMode - 1].temp;
#if USE_SIMULATED_DATA
  return TIMELINE[simIndex].temp;
#else
  return lastTemperature;                                 // PCT2075 temperature Wireling, deg C
#endif
}

int readLightLevel() {
  if (simMode) return PRESETS[simMode - 1].lux;
#if USE_SIMULATED_DATA
  return TIMELINE[simIndex].lux;
#else
  return lastLight;                                       // TSL2572 light Wireling, lux
#endif
}

// ================================================================ mood logic
Mood computeMood(float m, float t, int l) {
  if (m > MOISTURE_SOGGY)   return MOOD_SOGGY;    // soil problems first (the two can't both be true)
  if (m < MOISTURE_THIRSTY) return MOOD_THIRSTY;
  if (t < TEMP_COLD)        return MOOD_COLD;
  if (l < LIGHT_DARK)       return MOOD_SLEEPY;
  return MOOD_HAPPY;
}

uint8_t moodColor(Mood m) {
  switch (m) {
    case MOOD_THIRSTY: return RGB8(3, 5, 0);  // wilted yellow
    case MOOD_COLD:    return RGB8(1, 5, 7);  // icy blue
    case MOOD_SLEEPY:  return RGB8(0, 3, 1);  // dim green
    case MOOD_SOGGY:   return RGB8(0, 4, 4);  // waterlogged teal
    default:           return TS_8b_Green;
  }
}

// Least-squares slope of the last few moisture samples, in % per sample.
// Returns 0 (= "steady") until there are at least 3 samples.
float soilSlope() {
  uint8_t n = historyCount < 8 ? historyCount : 8;
  if (n < 3) return 0;
  float sx = 0, sy = 0, sxy = 0, sxx = 0;
  for (uint8_t i = 0; i < n; i++) {
    float y = histSoil[historyCount - n + i];
    sx += i; sy += y; sxy += i * y; sxx += i * i;
  }
  return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

// Hours until the soil is dry enough to need water.
// Returns <0 if no watering is predicted, 0 if it needs water now.
float predictHoursToWater() {
  if (moisture < MOISTURE_THIRSTY) return 0;
  float slope = soilSlope();
  if (slope > -0.05) return -1;
  return (moisture - MOISTURE_THIRSTY) / -slope * HOURS_PER_SAMPLE;
}

// Hours until an over-watered plant's soil drops back under the soggy limit.
// Returns <0 if the soil isn't drying out (yet).
float predictHoursToDry() {
  float slope = soilSlope();
  if (slope > -0.05) return -1;
  return (moisture - MOISTURE_SOGGY) / -slope * HOURS_PER_SAMPLE;
}

void pushHistory(float m, float t, float l) {
  if (historyCount == HISTORY_LEN) {
    for (uint8_t i = 1; i < HISTORY_LEN; i++) {
      histSoil[i - 1] = histSoil[i];
      histTemp[i - 1] = histTemp[i];
      histLight[i - 1] = histLight[i];
    }
    historyCount--;
  }
  histSoil[historyCount]  = m;
  histTemp[historyCount]  = t;
  histLight[historyCount] = l;
  historyCount++;
}

// ================================================================ drawing helpers
void formatClock(char *buf, size_t len) {
  if (simMode) {
    snprintf(buf, len, "DEMO");
  } else {
#if USE_SIMULATED_DATA
    uint16_t t = simulatedClock();
    snprintf(buf, len, "%02u:%02u%s", t / 60, t % 60, paused ? " ||" : "");
#else
    snprintf(buf, len, "LIVE%s", paused ? " ||" : "");   // no clock on the board: the dashboard stamps the time
#endif
  }
}

void drawViewDots() {
  for (uint8_t i = 0; i < VIEW_COUNT; i++)
    display.drawRect(79 + i * 3, 4, 2, 2, TSRectangleFilled, i == view ? C_TEXT : C_MUTED);
}

void drawHeader(const char *title, uint8_t color) {
  char clk[12];
  formatClock(clk, sizeof(clk));
  display.clearWindow(0, 0, 96, 10);
  display.setFont(thinPixel7_10ptFontInfo);
  display.fontColor(color, C_BG);
  display.setCursor(2, 0);
  display.print(title);
  display.fontColor(C_MUTED, C_BG);
  display.setCursor(78 - display.getPrintWidth(clk), 0);  // right-aligned, left of the page dots
  display.print(clk);
  drawViewDots();
}

// Horizontal progress bar with a threshold tick (and an optional second tick: pass thresh2 < 0 for none)
void drawHBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, float frac, float thresh, float thresh2, uint8_t color) {
  frac = constrain(frac, 0.0f, 1.0f);
  uint8_t fw = (uint8_t)(frac * w);
  display.drawRect(x, y, w, h, TSRectangleFilled, C_TRACK);
  if (fw) display.drawRect(x, y, fw, h, TSRectangleFilled, color);
  uint8_t tx = x + (uint8_t)(thresh * (w - 1));
  display.drawLine(tx, y - 1, tx, y + h - 1, C_TEXT);
  if (thresh2 >= 0) {
    uint8_t tx2 = x + (uint8_t)(thresh2 * (w - 1));
    display.drawLine(tx2, y - 1, tx2, y + h - 1, C_TEXT);
  }
}

void drawVBar(uint8_t x, uint8_t top, uint8_t w, uint8_t h, float frac, uint8_t color) {
  frac = constrain(frac, 0.0f, 1.0f);
  uint8_t fh = (uint8_t)(frac * h);
  if (h - fh) display.drawRect(x, top, w, h - fh, TSRectangleFilled, C_TRACK);
  if (fh) display.drawRect(x, top + h - fh, w, fh, TSRectangleFilled, color);
}

// Tiny 3x5 pixel font, A-Z, used for the small labels (S/T/L, SOIL, TEMP, LUX)
#define G3(r0, r1, r2, r3, r4) ((uint16_t)(((r0) << 12) | ((r1) << 9) | ((r2) << 6) | ((r3) << 3) | (r4)))
const uint16_t TINY_FONT[26] = {
  G3(2,5,7,5,5), G3(6,5,6,5,6), G3(3,4,4,4,3), G3(6,5,5,5,6), G3(7,4,6,4,7), G3(7,4,6,4,4), // A B C D E F
  G3(3,4,5,5,3), G3(5,5,7,5,5), G3(7,2,2,2,7), G3(1,1,1,5,2), G3(5,5,6,5,5), G3(4,4,4,4,7), // G H I J K L
  G3(5,7,7,5,5), G3(6,5,5,5,5), G3(2,5,5,5,2), G3(6,5,6,4,4), G3(2,5,5,7,3), G3(6,5,6,5,5), // M N O P Q R
  G3(3,4,2,1,6), G3(7,2,2,2,2), G3(5,5,5,5,7), G3(5,5,5,5,2), G3(5,5,7,7,5), G3(5,5,2,5,5), // S T U V W X
  G3(5,5,2,2,2), G3(7,1,2,4,7)                                                              // Y Z
};

uint8_t tinyTextWidth(const char *s) { return strlen(s) * 4 - 1; }

void drawTinyText(uint8_t x, uint8_t y, const char *s, uint8_t color) {
  for (; *s; s++, x += 4) {
    if (*s < 'A' || *s > 'Z') continue;
    uint16_t g = TINY_FONT[*s - 'A'];
    for (uint8_t row = 0; row < 5; row++)
      for (uint8_t col = 0; col < 3; col++)
        if ((g >> ((4 - row) * 3 + (2 - col))) & 1) display.drawPixel(x + col, y + row, color);
  }
}

float moistureFrac() { return moisture / 100.0f; }
float tempFrac()     { return temperature / 40.0f; }                    // 0-40 C
float lightFrac()    { return log10(lightLevel + 1.0f) / 3.0f; }       // 1-1000 lux, log scale

// ================================================================ PET view
// Layout: mini gauges x0-23, plant x30-66, effects x67-95
const uint8_t HEAD_X = 30, HEAD_Y = 18, HEAD_W = 36, HEAD_H = 28;

void drawMiniGauges() {
  float fr[3]  = { moistureFrac(), tempFrac(), lightFrac() };
  bool  ok[3]  = { moisture >= MOISTURE_THIRSTY && moisture <= MOISTURE_SOGGY,
                   temperature >= TEMP_COLD, lightLevel >= LIGHT_DARK };
  // one row per reading: icon (dimmed when out of range) + a small level bar next to it
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t y = 12 + i * 17;
    uint8_t bad = (i == 0 && moisture > MOISTURE_SOGGY) ? moodColor(MOOD_SOGGY) : C_BAD;
    drawTinyText(3, y + 5, i == 0 ? "S" : i == 1 ? "T" : "L", ok[i] ? C_TEXT : bad);   // S soil, T temperature, L light
    drawVBar(10, y, 7, 14, fr[i], ok[i] ? C_OK : bad);
  }
}

void drawEyes(uint8_t frame) {
  uint8_t hc = moodColor(mood);
  display.drawRect(34, 23, 28, 9, TSRectangleFilled, hc);
  switch (mood) {
    case MOOD_HAPPY:
      if (blinking) {
        display.drawLine(39, 28, 42, 28, C_FACE);
        display.drawLine(54, 28, 57, 28, C_FACE);
      } else {
        display.drawRect(39, 25, 4, 5, TSRectangleFilled, C_FACE);
        display.drawRect(54, 25, 4, 5, TSRectangleFilled, C_FACE);
        display.drawPixel(40, 26, C_TEXT);
        display.drawPixel(55, 26, C_TEXT);
      }
      break;
    case MOOD_THIRSTY:  // droopy half-closed eyes with worried brows
      display.drawRect(39, 28, 4, 3, TSRectangleFilled, C_FACE);
      display.drawRect(54, 28, 4, 3, TSRectangleFilled, C_FACE);
      display.drawLine(37, 26, 42, 24, C_FACE);
      display.drawLine(54, 24, 59, 26, C_FACE);
      break;
    case MOOD_COLD:     // squeezed > < eyes
      display.drawLine(39, 25, 42, 27, C_FACE);
      display.drawLine(42, 27, 39, 29, C_FACE);
      display.drawLine(57, 25, 54, 27, C_FACE);
      display.drawLine(54, 27, 57, 29, C_FACE);
      break;
    case MOOD_SLEEPY:   // closed eyes
      display.drawLine(39, 29, 42, 29, C_FACE);
      display.drawPixel(38, 28, C_FACE);
      display.drawPixel(43, 28, C_FACE);
      display.drawLine(54, 29, 57, 29, C_FACE);
      display.drawPixel(53, 28, C_FACE);
      display.drawPixel(58, 28, C_FACE);
      break;
    case MOOD_SOGGY:    // bulging eyes, pupils looking down at the puddle
      display.drawRect(38, 24, 6, 6, TSRectangleFilled, C_TEXT);
      display.drawRect(53, 24, 6, 6, TSRectangleFilled, C_TEXT);
      display.drawRect(40, 27, 2, 3, TSRectangleFilled, C_FACE);
      display.drawRect(55, 27, 2, 3, TSRectangleFilled, C_FACE);
      break;
    default: break;
  }
}

void drawMouth(uint8_t frame) {
  display.drawRect(41, 34, 15, 7, TSRectangleFilled, moodColor(mood));
  switch (mood) {
    case MOOD_HAPPY:
      display.drawLine(43, 35, 45, 37, C_FACE);
      display.drawLine(45, 37, 51, 37, C_FACE);
      display.drawLine(51, 37, 53, 35, C_FACE);
      break;
    case MOOD_THIRSTY:
      display.drawLine(43, 39, 45, 37, C_FACE);
      display.drawLine(45, 37, 51, 37, C_FACE);
      display.drawLine(51, 37, 53, 39, C_FACE);
      break;
    case MOOD_COLD: {   // chattering zig-zag, flips each frame
      uint8_t px = 42, py = 37;
      for (uint8_t x = 44; x <= 54; x += 2) {
        uint8_t y = ((x / 2 + frame) & 1) ? 35 : 38;
        display.drawLine(px, py, x, y, C_FACE);
        px = x; py = y;
      }
      break;
    }
    case MOOD_SLEEPY:
      display.drawRect(47, 36, 3, 3, TSRectangleNoFill, C_FACE);
      break;
    case MOOD_SOGGY:    // open mouth, gasping
      display.drawRect(45, 35, 7, 5, TSRectangleFilled, C_FACE);
      break;
    default: break;
  }
}

void drawLeaves() {
  display.clearWindow(36, 10, 26, 8);
  uint8_t leaf = (mood == MOOD_SOGGY) ? C_LEAF_ROT : C_LEAF;   // over-watered leaves go olive
  display.drawLine(48, 11, 48, 17, leaf);
  if (mood == MOOD_THIRSTY || mood == MOOD_SOGGY) {  // limp leaves droop down
    display.drawLine(47, 12, 41, 16, leaf);
    display.drawLine(47, 13, 42, 17, leaf);
    display.drawLine(49, 12, 55, 16, leaf);
    display.drawLine(49, 13, 54, 17, leaf);
  } else {
    display.drawRect(41, 13, 6, 3, TSRectangleFilled, leaf);
    display.drawRect(49, 11, 6, 3, TSRectangleFilled, leaf);
  }
}

void drawPlant() {
  uint8_t hc = moodColor(mood);
  drawLeaves();
  // head with rounded corners
  display.drawRect(HEAD_X, HEAD_Y, HEAD_W, HEAD_H, TSRectangleFilled, hc);
  display.drawPixel(HEAD_X, HEAD_Y, C_BG);
  display.drawPixel(HEAD_X + HEAD_W - 1, HEAD_Y, C_BG);
  display.drawPixel(HEAD_X, HEAD_Y + HEAD_H - 1, C_BG);
  display.drawPixel(HEAD_X + HEAD_W - 1, HEAD_Y + HEAD_H - 1, C_BG);
  // cheeks
  if (mood == MOOD_HAPPY) {
    display.drawRect(34, 33, 3, 2, TSRectangleFilled, C_CHEEK);
    display.drawRect(59, 33, 3, 2, TSRectangleFilled, C_CHEEK);
  }
  // pot
  display.drawRect(32, 47, 32, 4, TSRectangleFilled, C_POT_RIM);
  for (uint8_t i = 0; i < 10; i++)
    display.drawLine(34 + i / 3, 51 + i, 61 - i / 3, 51 + i, C_POT);
  // puddle of excess water under the pot
  if (mood == MOOD_SOGGY) display.drawRect(24, 61, 48, 3, TSRectangleFilled, C_WATER);
}

// Small per-frame effects around the plant
void drawEffects(uint8_t frame) {
  switch (mood) {
    case MOOD_HAPPY: {
      bool shouldBlink = (frame % 14) == 0;
      if (shouldBlink != blinking) { blinking = shouldBlink; drawEyes(frame); }
      break;
    }
    case MOOD_THIRSTY: {  // sweat drop sliding down the side of the head
      display.clearWindow(67, 16, 6, 32);
      uint8_t y = 18 + (frame % 6) * 4;
      display.drawPixel(69, y, C_WATER);
      display.drawRect(68, y + 1, 3, 3, TSRectangleFilled, C_WATER);
      break;
    }
    case MOOD_COLD: {     // shiver lines alternate sides, teeth chatter
      display.clearWindow(25, 20, 4, 24);
      display.clearWindow(67, 20, 4, 24);
      uint8_t off = (frame & 1) ? 0 : 2;
      display.drawLine(26 + off / 2, 22 + off, 26 + off / 2, 28 + off, C_WATER);
      display.drawLine(26 + off / 2, 32 + off, 26 + off / 2, 38 + off, C_WATER);
      display.drawLine(69 - off / 2, 22 + off, 69 - off / 2, 28 + off, C_WATER);
      display.drawLine(69 - off / 2, 32 + off, 69 - off / 2, 38 + off, C_WATER);
      drawMouth(frame);
      break;
    }
    case MOOD_SLEEPY: {   // Zzz floating up
      display.clearWindow(68, 11, 28, 34);
      display.fontColor(C_MUTED, C_BG);
      uint8_t step = frame % 4;
      display.setCursor(70, 34 - step * 3);  display.print("z");
      if (step >= 1) { display.setCursor(77, 28 - step * 3); display.print("z"); }
      if (step >= 2) { display.fontColor(C_TEXT, C_BG); display.setCursor(84, 22 - step * 3); display.print("Z"); }
      break;
    }
    case MOOD_SOGGY: {    // water drips off both sides, the puddle ripples
      display.clearWindow(25, 14, 6, 44);
      display.clearWindow(66, 14, 6, 44);
      uint8_t yl = 16 + (frame % 7) * 6;
      uint8_t yr = 16 + ((frame + 3) % 7) * 6;
      display.drawPixel(27, yl, C_WATER);       display.drawRect(26, yl + 1, 3, 3, TSRectangleFilled, C_WATER);
      display.drawPixel(69, yr, C_WATER);       display.drawRect(68, yr + 1, 3, 3, TSRectangleFilled, C_WATER);
      display.drawRect(24, 61, 48, 3, TSRectangleFilled, C_WATER);
      display.drawLine(26 + (frame * 7) % 40, 62, 31 + (frame * 7) % 40, 62, C_TEXT);
      break;
    }
    default: break;
  }
}

void drawPetView() {
  display.clearScreen();
  drawHeader(MOOD_NAMES[mood], moodColor(mood));
  drawMiniGauges();
  blinking = false;
  drawPlant();
  drawEyes(animFrame);
  drawMouth(animFrame);
  drawEffects(animFrame);
}

// ================================================================ VITALS view
void drawVitalRow(uint8_t y, const char *label, const char *value, bool ok,
                  const char *badText, float frac, float thresh, float thresh2, uint8_t badColor) {
  display.clearWindow(0, y, 96, 14);
  drawTinyText(2, y + 4, label, ok ? C_MUTED : badColor);
  display.fontColor(C_TEXT, C_BG);
  display.setCursor(20, y + 2);
  display.print(value);
  display.fontColor(ok ? C_OK : badColor, C_BG);
  display.setCursor(74, y + 2);
  display.print(ok ? "OK" : badText);
  drawHBar(2, y + 14, 92, 3, frac, thresh, thresh2, ok ? C_OK : badColor);
}

void drawVitalsView(bool full) {
  if (full) display.clearScreen();
  drawHeader("VITALS", C_TEXT);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d %%", (int)moisture);
  bool wet = moisture > MOISTURE_SOGGY;
  drawVitalRow(12, "SOIL", buf, moisture >= MOISTURE_THIRSTY && !wet, wet ? "WET" : "DRY",
               moistureFrac(), MOISTURE_THIRSTY / 100.0f, MOISTURE_SOGGY / 100.0f,   // two ticks: healthy band
               wet ? moodColor(MOOD_SOGGY) : C_BAD);
  snprintf(buf, sizeof(buf), "%d.%d C", (int)temperature, (int)(temperature * 10) % 10);
  drawVitalRow(29, "TEMP", buf, temperature >= TEMP_COLD, "COLD",
               tempFrac(), TEMP_COLD / 40.0f, -1, moodColor(MOOD_COLD));
  snprintf(buf, sizeof(buf), "%d lx", lightLevel);
  drawVitalRow(46, "LUX", buf, lightLevel >= LIGHT_DARK, "DARK",
               lightFrac(), log10(LIGHT_DARK + 1.0f) / 3.0f, -1, C_MUTED);
}

// ================================================================ GRAPH views (soil / temp / light)
const uint8_t G_X = 14, G_Y = 12, G_W = 80, G_H = 38;  // graph box

struct GraphInfo {
  const char *title;     // header text
  const char *topLabel;  // y-axis label at the top of the graph
  const char *botLabel;  // y-axis label at the bottom
  uint8_t     lineColor;
  float       threshold; // below this value the plant is unhappy
  float       upper;     // above this value it is unhappy too (< 0 = no upper limit)
};

const GraphInfo GRAPHS[MET_COUNT] = {
  { "SOIL",  "100", "0",  C_WATER,        MOISTURE_THIRSTY, MOISTURE_SOGGY },
  { "TEMP",  "35",  "10", RGB8(3, 3, 0),  TEMP_COLD,        -1 },
  { "LIGHT", "1k",  "1",  TS_8b_Yellow,   (float)LIGHT_DARK, -1 },
};

// Map a value to 0..1 on the graph's vertical axis
float graphFrac(uint8_t metric, float v) {
  float f;
  if (metric == MET_SOIL)      f = v / 100.0f;                 // 0-100 %
  else if (metric == MET_TEMP) f = (v - 10.0f) / 25.0f;        // 10-35 C
  else                         f = log10(v + 1.0f) / 3.0f;     // 1-1000 lx, log scale
  return constrain(f, 0.0f, 1.0f);
}

uint8_t graphY(uint8_t metric, float v) {
  return G_Y + G_H - 1 - (uint8_t)(graphFrac(metric, v) * (G_H - 1));
}

// Bottom line: soil shows the watering prediction, temp/light show the current value + status
void drawGraphCaption(uint8_t metric) {
  char buf[24];
  uint8_t color = C_TEXT;
  if (metric == MET_SOIL && moisture > MOISTURE_SOGGY) {
    float d = predictHoursToDry();
    if (d < 0) snprintf(buf, sizeof(buf), "Too wet! Wait");
    else       snprintf(buf, sizeof(buf), "Dry in ~%d.%dh", (int)d, (int)(d * 10) % 10);
    color = moodColor(MOOD_SOGGY);
  } else if (metric == MET_SOIL) {
    float h = predictHoursToWater();
    if (h == 0)      { snprintf(buf, sizeof(buf), "Water me now!"); color = C_BAD; }
    else if (h < 0)  { snprintf(buf, sizeof(buf), "Moisture steady"); color = C_OK; }
    else             { snprintf(buf, sizeof(buf), "Water in ~%d.%dh", (int)h, (int)(h * 10) % 10); }
  } else if (metric == MET_TEMP) {
    bool ok = temperature >= TEMP_COLD;
    snprintf(buf, sizeof(buf), "%d.%d C  %s", (int)temperature, (int)(temperature * 10) % 10, ok ? "OK" : "COLD");
    color = ok ? C_OK : C_BAD;
  } else {
    bool ok = lightLevel >= LIGHT_DARK;
    snprintf(buf, sizeof(buf), "%d lx  %s", lightLevel, ok ? "OK" : "DARK");
    color = ok ? C_OK : C_BAD;
  }
  bool ok = metric == MET_SOIL ? (moisture >= MOISTURE_THIRSTY && moisture <= MOISTURE_SOGGY)
          : metric == MET_TEMP ? temperature >= TEMP_COLD
          : lightLevel >= LIGHT_DARK;
  display.clearWindow(0, 50, 96, 14);
  drawTinyText(1, 54, metric == MET_SOIL ? "SOIL" : metric == MET_TEMP ? "TEMP" : "LUX", ok ? C_MUTED : color);
  display.fontColor(color, C_BG);
  display.setCursor(18, 52);
  display.print(buf);
}

void drawGraphView(uint8_t metric, bool full) {
  const GraphInfo &g = GRAPHS[metric];
  float *data = historyFor(metric);
  if (full) display.clearScreen();
  drawHeader(g.title, g.lineColor);

  // y-axis labels (clear the label column first: label widths differ between graphs)
  display.clearWindow(0, G_Y - 1, G_X - 1, G_H + 2);
  display.fontColor(C_MUTED, C_BG);
  display.setCursor(0, G_Y - 1);
  display.print(g.topLabel);
  display.setCursor(0, G_Y + G_H - 9);
  display.print(g.botLabel);

  // plot area, axis and dashed threshold line
  display.clearWindow(G_X, G_Y, G_W, G_H);
  display.drawLine(G_X - 1, G_Y, G_X - 1, G_Y + G_H - 1, C_TRACK);
  uint8_t ty = graphY(metric, g.threshold);
  for (uint8_t x = G_X; x < G_X + G_W; x += 3) display.drawPixel(x, ty, C_BAD);
  if (g.upper >= 0) {   // second limit, drawn in the soggy colour with a different dash
    uint8_t uy = graphY(metric, g.upper);
    for (uint8_t x = G_X; x < G_X + G_W; x += 4) display.drawLine(x, uy, x + 1, uy, moodColor(MOOD_SOGGY));
  }

  // data line: red below the lower limit, teal above the upper limit
  for (uint8_t i = 1; i < historyCount; i++) {
    uint8_t x0 = G_X + (i - 1) * 2, x1 = G_X + i * 2;
    uint8_t c = data[i] < g.threshold ? C_BAD
              : (g.upper >= 0 && data[i] > g.upper) ? moodColor(MOOD_SOGGY)
              : g.lineColor;
    display.drawLine(x0, graphY(metric, data[i - 1]), x1, graphY(metric, data[i]), c);
  }

  drawGraphCaption(metric);
}

// ================================================================ view control
void redrawView(bool full) {
  switch (view) {
    case VIEW_PET:
      if (full) drawPetView();
      else { drawHeader(MOOD_NAMES[mood], moodColor(mood)); drawMiniGauges(); }
      break;
    case VIEW_VITALS: drawVitalsView(full); break;
    case VIEW_GRAPH_SOIL:  drawGraphView(MET_SOIL, full);  break;
    case VIEW_GRAPH_TEMP:  drawGraphView(MET_TEMP, full);  break;
    case VIEW_GRAPH_LIGHT: drawGraphView(MET_LIGHT, full); break;
    default: break;
  }
}

void wake() {
  lastActivityMs = millis();
  if (dimmed) { display.setBrightness(BRIGHT_NORMAL); dimmed = false; }
}

// ---------------------------------------------------------------- WiFi (battery / no-USB mode)
#if USE_WIFI && !USE_SIMULATED_DATA
static bool          wifiUp        = false;
static unsigned long wifiRetryAtMs = 0;       // earliest time we may try (re)connecting or sending again
static uint8_t       wifiFails     = 0;       // joins/sends that failed in a row; after 3 the WiFi module is power-cycled
const unsigned long  WIFI_RETRY_MS = 10000;   // wait between attempts after a failure (a join can block the screen for ~8 s)

// Join the network. reset = true power-cycles the WiFi module first, which recovers a link that got stuck.
static void wifiConnect(bool reset) {
  if (reset) {
    SerialUSB.print(F("# wifi: resetting the WiFi module at ")); SerialUSB.print(millis() / 1000); SerialUSB.println(F(" s"));
    WiFi.end();
    delay(200);
    WiFi.setPins(8, 2, A3, -1);
  }
  SerialUSB.println(F("# wifi: joining network..."));
  WiFi.begin(WIFI_SSID, WIFI_PASS);           // blocks until connected or the timeout set in setup()
  wifiUp = (WiFi.status() == WL_CONNECTED);
  wifiRetryAtMs = millis() + WIFI_RETRY_MS;
  SerialUSB.print(F("# wifi: "));
  if (wifiUp) { wifiFails = 0; WiFi.lowPowerMode(); SerialUSB.print(F("connected, my IP ")); SerialUSB.println(IPAddress(WiFi.localIP())); }
  else { if (wifiFails < 255) wifiFails++; SerialUSB.print(F("NOT connected, status code ")); SerialUSB.println(WiFi.status()); }   // 1=no shield found, 4=connect failed, 6=disconnected
}

static void wifiFail(const __FlashStringHelper* why) {
  if (wifiFails < 255) wifiFails++;
  wifiUp = false;
  wifiRetryAtMs = millis() + WIFI_RETRY_MS;
  SerialUSB.print(F("# wifi: ")); SerialUSB.print(why);
  SerialUSB.print(F(" (")); SerialUSB.print(wifiFails); SerialUSB.print(F(" in a row, at ")); SerialUSB.print(millis() / 1000); SerialUSB.println(F(" s)"));
}

// The server answers each reading with the plant's limits as "soilLo,soilHi,temp,lux". Adopt them so the face
// judges the plant by the same limits as the dashboard. Ignores anything that doesn't look right.
static void applyLimits(const String& body) {
  int lo, hi, t, l;
  if (sscanf(body.c_str(), "%d,%d,%d,%d", &lo, &hi, &t, &l) != 4) return;
  if (lo < 0 || hi > 100 || lo >= hi || t < 0 || t > 40 || l < 1 || l > 1000) return;
  if (lo == (int)MOISTURE_THIRSTY && hi == (int)MOISTURE_SOGGY && t == (int)TEMP_COLD && l == LIGHT_DARK) return;   // unchanged
  MOISTURE_THIRSTY = lo; MOISTURE_SOGGY = hi; TEMP_COLD = t; LIGHT_DARK = l;
  SerialUSB.print(F("# plant limits from dashboard: soil ")); SerialUSB.print(lo); SerialUSB.print('-'); SerialUSB.print(hi);
  SerialUSB.print(F("%, min ")); SerialUSB.print(t); SerialUSB.print(F(" C, min ")); SerialUSB.print(l); SerialUSB.println(F(" lx"));
}

// POST one CSV reading line to the dashboard server and check the server accepted it.
// Never keeps the sketch waiting for long, and recovers by itself when the link drops.
static void wifiSend(const String& line) {
  if (!wifiUp && millis() < wifiRetryAtMs) return;
  // rejoin if the shield says it is disconnected, or if sends keep failing even though it says "connected" (a silently dead link)
  if (wifiFails >= 3 || WiFi.status() != WL_CONNECTED) {
    wifiUp = false;
    wifiConnect(wifiFails >= 3);
    if (!wifiUp) return;
  }

  WiFiClient client;
  if (!client.connect(IPAddress(SERVER_IP), SERVER_PORT)) { client.stop(); wifiFail(F("cannot reach the dashboard server")); return; }
  client.print(F("POST /api/ingest HTTP/1.1\r\nHost: plantguardian\r\nX-Token: "));
  client.print(F(DEVICE_TOKEN));
  client.print(F("\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: "));
  client.print(line.length());
  client.print(F("\r\n\r\n"));
  client.print(line);
  unsigned long t = millis();                  // wait briefly for the reply so we know the data really arrived
  while (!client.available() && millis() - t < 1500) delay(5);
  String status = client.available() ? client.readStringUntil('\n') : String();
  bool hasLimits = status.indexOf(F(" 200")) >= 0;           // 200 = accepted + the plant's limits in the body; 204 = accepted, no limits set yet
  String body;
  if (hasLimits) {
    for (uint8_t i = 0; i < 10; i++) { String h = client.readStringUntil('\n'); if (h.length() <= 1) break; }   // skip the reply headers
    body = client.readStringUntil('\n');
  }
  client.stop();
  if (!hasLimits && status.indexOf(F(" 204")) < 0) { wifiFail(status.length() ? F("server rejected the reading (check DEVICE_TOKEN)") : F("no reply from the server")); return; }
  wifiUp = true;
  wifiFails = 0;
  if (hasLimits) applyLimits(body);
}
#endif

// Why the chip last restarted, read from the SAMD21 reset-cause register. BOD33 / POWER-ON on battery = the supply sagged.
static const char* resetCauseName() {
  uint8_t c = PM->RCAUSE.reg;
  if (c & PM_RCAUSE_BOD33) return "BOD33";
  if (c & PM_RCAUSE_BOD12) return "BOD12";
  if (c & PM_RCAUSE_POR)   return "POWER-ON";
  if (c & PM_RCAUSE_WDT)   return "WDT";
  if (c & PM_RCAUSE_SYST)  return "SOFT";
  if (c & PM_RCAUSE_EXT)   return "RESET-BTN";
  return "?";
}

void takeReading() {
#if !USE_SIMULATED_DATA
  refreshSensors();
#endif
  moisture    = readSoilMoisture();
  temperature = readTemperature();
  lightLevel  = readLightLevel();
  pushHistory(moisture, temperature, lightLevel);

  Mood newMood = computeMood(moisture, temperature, lightLevel);
  bool moodChanged = newMood != mood;
  mood = newMood;
  if (moodChanged) wake();

  // Build the CSV line once; it goes to USB serial and (if enabled) over WiFi.
  String line;
#if USE_SIMULATED_DATA
  line += simulatedClock();                                  // sample clock, minutes since midnight
#else
  static unsigned long readingSeq = 0;
  line += readingSeq++;                                      // reading counter (the dashboard stamps the real time)
#endif
  line += ','; line += String(moisture);
  line += ','; line += String(temperature);
  line += ','; line += lightLevel;
  line += ','; line += MOOD_NAMES[mood];
#if !USE_SIMULATED_DATA
  // sensor health + whether this reading is real or a demo preset, for the dashboard
  line += moistureOk ? F(",Mok") : F(",MERR");
  line += tempOk     ? F(",Tok") : F(",TERR");
  line += lightOk    ? F(",Lok") : F(",LERR");
  line += simMode    ? F(",PRESET") : F(",LIVE");
  static uint8_t bootTag = 12;    // on the first readings after a restart, say why it restarted; the dashboard server logs it
  if (bootTag) { bootTag--; line += F(",R:"); line += resetCauseName(); }
#endif
  SerialUSB.println(line);

  redrawView(moodChanged && view == VIEW_PET);     // draw first, then send, so the screen never waits on the network
#if USE_WIFI && !USE_SIMULATED_DATA
  wifiSend(line);
#endif

#if USE_SIMULATED_DATA
  if (simMode == 0 && !paused) simIndex = (simIndex + 1) % TIMELINE_LEN;
#endif
}

void handleButtons() {
  uint8_t b = display.getButtons();
  uint8_t pressed = b & ~lastButtons;
  lastButtons = b;
  if (!pressed) return;
  wake();

  if (pressed & TSButtonLowerRight) { view = (View)((view + 1) % VIEW_COUNT); redrawView(true); }
  if (pressed & TSButtonLowerLeft)  { view = (View)((view + VIEW_COUNT - 1) % VIEW_COUNT); redrawView(true); }
  if (pressed & TSButtonUpperLeft)  {
    simMode = (simMode + 1) % (MOOD_COUNT + 1);
    takeReading();
    redrawView(true);
    lastSensorMs = millis();
  }
  if (pressed & TSButtonUpperRight) { paused = !paused; redrawView(false); }
}

// ================================================================ setup / loop
void setup() {
  Wire.begin();
  SerialUSB.begin(9600);

  display.begin();
  display.setFlip(true);                 // the board is mounted so the screen needs a 180 degree turn
  display.setBrightness(BRIGHT_NORMAL);
  display.setFont(thinPixel7_10ptFontInfo);
  display.clearScreen();

#if !USE_SIMULATED_DATA
  // Sensors come up after the display, with no long delays, so a missing sensor can't freeze the boot.
  Wireling.begin();                  // the adapter's I2C multiplexer
  Wireling.selectPort(LIGHT_PORT);
  tsl2572Begin();                    // light sensor needs a one-time setup; temperature and moisture don't
#endif

#if USE_WIFI && !USE_SIMULATED_DATA
  WiFi.setPins(8, 2, A3, -1);        // WiFi TinyShield pin map (from TinyCircuits' tutorial); must come before any other WiFi call
  WiFi.setTimeout(8000);             // give up on a missing network after 8 s instead of the default
  wifiRetryAtMs = millis() + 8000;   // first connect happens in loop(), after the screen is up, so a stuck network can't leave it blank
#endif

  takeReading();
  redrawView(true);
  lastSensorMs = lastAnimMs = lastActivityMs = millis();
}

// Same structure as the plan (read -> mood -> render every 2 s), but driven by
// millis() instead of delay(2000) so the buttons and animations stay responsive.
void loop() {
  unsigned long now = millis();

  handleButtons();

  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
#if USE_SIMULATED_DATA
    takeReading();
#else
    if (!paused) takeReading();      // pause (upper-right) freezes the real readings too
#endif
  }

  if (now - lastAnimMs >= ANIM_INTERVAL_MS) {
    lastAnimMs = now;
    animFrame++;
    if (view == VIEW_PET) drawEffects(animFrame);
  }

  // Power saving: dim the OLED when the mood hasn't changed for a while
  if (!dimmed && now - lastActivityMs > DIM_AFTER_MS) {
    display.setBrightness(BRIGHT_DIM);
    dimmed = true;
  }

  delay(10);
}

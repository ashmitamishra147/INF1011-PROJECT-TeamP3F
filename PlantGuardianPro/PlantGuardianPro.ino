/*
 * PlantGuardianPro.ino
 *
 * Pro variant of PlantGuardian, tuned for an ALOE VERA and extended so that
 * every environmental preset has its own emoticon (mood face).
 *
 * Aloe Vera care thresholds used here:
 *                 Too Low            Optimal              Too High
 *   Soil moisture dry (water me)     drying / drained     persistently wet
 *   Temperature   below 13 C         13-27 C              above 30 C
 *   Luminance     below 1000 lux     5000-10000 lux       above 20000 lux
 *
 * Moods / emoticons (one per environmental state in the table above):
 *   HAPPY   - everything in the optimal band
 *   THIRSTY - soil too dry            (soil too low)
 *   SOGGY   - soil persistently wet   (soil too high)
 *   COLD    - below 13 C              (temp too low)
 *   HOT     - above 30 C              (temp too high)   <-- new in Pro
 *   SLEEPY  - below 1000 lux          (light too low)
 *   BRIGHT  - above 20000 lux         (light too high)  <-- new in Pro
 *
 * Press the UPPER-LEFT button to cycle the demo presets: LIVE -> HAPPY ->
 * THIRSTY -> SOGGY -> COLD -> HOT -> SLEEPY -> BRIGHT -> LIVE ... Each preset
 * drives the sensors to values in that band so the matching emoticon shows.
 */

#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>

// ---- Wireling adapter ports (must be defined before Sensors.h) -------------
// Your wiring: light=0, moisture=1, temp=2, buzzer=3.
#define LIGHT_PORT    0
#define MOISTURE_PORT 1
#define TEMP_PORT     2
#define BUZZER_PORT   3

#include "sprites.h"
#include "Sensors.h"      // live Wireling drivers (PCT2075, TSL2572, moisture)


// 1 = use built-in simulated timeline/presets, 0 = read the real Wirelings.
#define USE_SIMULATED_DATA 0

// ---- Buzzer (AST1035 on port 3, driven as a digital pin) -------------------
// On a TinyScreen+ the four Wireling ports map to pins A0..A3; port 3 is A3.
const uint8_t BUZZER_PIN  = A3;
const int     BUZZER_FREQ = 2000;    // Hz
const bool    BUZZER_ON_DRY = true;  // chirp when soil reads THIRSTY

const unsigned long SENSOR_INTERVAL_MS = 2000;
const unsigned long ANIM_INTERVAL_MS   = 350;
const unsigned long DIM_AFTER_MS       = 30000;
const uint8_t BRIGHT_NORMAL = 10;
const uint8_t BRIGHT_DIM    = 3;


// ---- Aloe Vera thresholds --------------------------------------------------
// Soil moisture is a relative 0-100% scale (sensor %), not lux/temperature.
const float MOISTURE_THIRSTY = 25.0;    // below this: dry, water needed
const float MOISTURE_SOGGY   = 80.0;    // above this: persistently wet
const float TEMP_COLD        = 13.0;    // below this: too cold
const float TEMP_HOT         = 30.0;    // above this: too hot
const int   LIGHT_DARK       = 1000;    // below this: too dark
const long  LIGHT_BRIGHT     = 20000;   // above this: too bright


const float HOURS_PER_SAMPLE = 0.5;


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
const uint8_t C_LEAF_ROT = RGB8(1, 3, 0);
const uint8_t C_CHEEK    = RGB8(3, 2, 5);
const uint8_t C_WATER    = RGB8(1, 6, 7);
const uint8_t C_SUN      = RGB8(3, 6, 0);   // hot / bright accent


enum Mood { MOOD_HAPPY, MOOD_THIRSTY, MOOD_COLD, MOOD_SLEEPY, MOOD_SOGGY,
            MOOD_HOT, MOOD_BRIGHT, MOOD_COUNT };
enum View { VIEW_BUDDY, VIEW_PET, VIEW_VITALS, VIEW_GRAPH_SOIL, VIEW_GRAPH_TEMP, VIEW_GRAPH_LIGHT, VIEW_COUNT };
enum Metric { MET_SOIL, MET_TEMP, MET_LIGHT, MET_COUNT };

struct Sample {
  uint16_t minuteOfDay;
  uint8_t  moisture;
  float    temp;
  uint16_t lux;
};

struct Preset {
  float moisture;
  float temp;
  int   lux;
};

const char *MOOD_NAMES[MOOD_COUNT] = { "HAPPY", "THIRSTY", "COLD", "SLEEPY", "SOGGY", "HOT", "BRIGHT" };


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


// One preset per mood, in the SAME order as the Mood enum, so cycling simMode
// shows the emoticon for each environmental state. Each puts exactly one
// reading out of the Aloe Vera optimal band (and the rest inside it).
const Preset PRESETS[MOOD_COUNT] = {
  { 55, 22.0,  7000 },   // HAPPY   : all optimal
  { 15, 22.0,  7000 },   // THIRSTY : soil too dry
  { 55, 10.0,  7000 },   // COLD    : temp too low
  { 55, 22.0,   300 },   // SLEEPY  : light too low
  { 92, 22.0,  7000 },   // SOGGY   : soil too wet
  { 55, 34.0,  7000 },   // HOT     : temp too high
  { 55, 22.0, 26000 },   // BRIGHT  : light too high
};


TinyScreen display = TinyScreen(TinyScreenPlus);

float moisture = 0, temperature = 0;
int   lightLevel = 0;
Mood  mood = MOOD_HAPPY;
View  view = VIEW_BUDDY;

uint8_t simIndex = 0;
uint8_t simMode  = 0;
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



uint16_t simulatedClock() { return TIMELINE[simIndex].minuteOfDay; }

// Cache of the most recent live reading from the Wirelings. takeReading()
// refreshes this once per cycle (when not showing a preset), and the three
// read* helpers below serve from it so each sensor is polled only once.
SensorReadings live;
bool liveMoistureOk = false, liveTempOk = false, liveLightOk = false;
// Last known-good values, so a momentary sensor hiccup doesn't blank the UI.
float lastMoisture = 50, lastTemperature = 22;
int   lastLight = 5000;

// Pull one fresh sample from the real sensors into the cache.
void refreshLive() {
  live = sensorsRead();
  liveMoistureOk = live.moistureOk;
  liveTempOk     = live.tempOk;
  liveLightOk    = live.lightOk;
  if (liveMoistureOk) lastMoisture    = live.moisturePct;
  if (liveTempOk)     lastTemperature = live.tempC;
  if (liveLightOk)    lastLight       = (int)(live.lux + 0.5f);
}

float readSoilMoisture() {
  if (simMode) return PRESETS[simMode - 1].moisture;   // preset demo override
#if USE_SIMULATED_DATA
  return TIMELINE[simIndex].moisture;
#else
  return lastMoisture;   // last good live reading (0-100%)
#endif
}

float readTemperature() {
  if (simMode) return PRESETS[simMode - 1].temp;
#if USE_SIMULATED_DATA
  return TIMELINE[simIndex].temp;
#else
  return lastTemperature;
#endif
}

int readLightLevel() {
  if (simMode) return PRESETS[simMode - 1].lux;
#if USE_SIMULATED_DATA
  return TIMELINE[simIndex].lux;
#else
  return lastLight;
#endif
}


// Priority order: the most urgent/hazardous state wins when several are off.
Mood computeMood(float m, float t, int l) {
  if (m > MOISTURE_SOGGY)   return MOOD_SOGGY;
  if (m < MOISTURE_THIRSTY) return MOOD_THIRSTY;
  if (t > TEMP_HOT)         return MOOD_HOT;
  if (t < TEMP_COLD)        return MOOD_COLD;
  if (l > LIGHT_BRIGHT)     return MOOD_BRIGHT;
  if (l < LIGHT_DARK)       return MOOD_SLEEPY;
  return MOOD_HAPPY;
}

uint8_t moodColor(Mood m) {
  switch (m) {
    case MOOD_THIRSTY: return RGB8(3, 5, 0);
    case MOOD_COLD:    return RGB8(1, 5, 7);
    case MOOD_SLEEPY:  return RGB8(0, 3, 1);
    case MOOD_SOGGY:   return RGB8(0, 4, 4);
    case MOOD_HOT:     return RGB8(3, 2, 0);   // hot orange/red
    case MOOD_BRIGHT:  return RGB8(3, 6, 1);   // glaring yellow-green
    default:           return TS_8b_Green;
  }
}


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


float predictHoursToWater() {
  if (moisture < MOISTURE_THIRSTY) return 0;
  float slope = soilSlope();
  if (slope > -0.05) return -1;
  return (moisture - MOISTURE_THIRSTY) / -slope * HOURS_PER_SAMPLE;
}


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


void formatClock(char *buf, size_t len) {
  if (simMode) {
    snprintf(buf, len, "DEMO");
  } else {
#if USE_SIMULATED_DATA
    uint16_t t = simulatedClock();
    snprintf(buf, len, "%02u:%02u%s", t / 60, t % 60, paused ? " ||" : "");
#else
    snprintf(buf, len, "LIVE");   // reading real sensors
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
  display.setCursor(78 - display.getPrintWidth(clk), 0);
  display.print(clk);
  drawViewDots();
}


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


uint8_t dimColor(uint8_t c) { return (uint8_t)(((c & 0xC0) >> 1) | ((c & 0x18) >> 1) | ((c >> 1) & 1)); }


#define G3(r0, r1, r2, r3, r4) ((uint16_t)(((r0) << 12) | ((r1) << 9) | ((r2) << 6) | ((r3) << 3) | (r4)))
const uint16_t TINY_FONT[26] = {
  G3(2,5,7,5,5), G3(6,5,6,5,6), G3(3,4,4,4,3), G3(6,5,5,5,6), G3(7,4,6,4,7), G3(7,4,6,4,4),
  G3(3,4,5,5,3), G3(5,5,7,5,5), G3(7,2,2,2,7), G3(1,1,1,5,2), G3(5,5,6,5,5), G3(4,4,4,4,7),
  G3(5,7,7,5,5), G3(6,5,5,5,5), G3(2,5,5,5,2), G3(6,5,6,4,4), G3(2,5,5,7,3), G3(6,5,6,5,5),
  G3(3,4,2,1,6), G3(7,2,2,2,2), G3(5,5,5,5,7), G3(5,5,5,5,2), G3(5,5,7,7,5), G3(5,5,2,5,5),
  G3(5,5,2,2,2), G3(7,1,2,4,7)
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
float tempFrac()     { return temperature / 40.0f; }
float lightFrac()    { return log10(lightLevel + 1.0f) / log10((float)LIGHT_BRIGHT + 1.0f); }

void drawBuddyView(bool full) {

  static uint8_t top[4], bot[4];
  static bool measured = false;
  if (!measured) {
    for (uint8_t r = 1; r < 4; r++) { top[r] = 255; bot[r] = 0; }
    for (uint16_t i = 0; i < (uint16_t)SCENE_W * SCENE_H; i++) {
      uint8_t r = SCENE_REG[i], y = i / SCENE_W;
      if (!r) continue;
      if (y < top[r]) top[r] = y;
      if (y > bot[r]) bot[r] = y;
    }
    measured = true;
  }
  float fr[4] = { 0, constrain(moistureFrac(), 0, 1), constrain(tempFrac(), 0, 1), constrain(lightFrac(), 0, 1) };
  uint8_t level[4];
  for (uint8_t r = 1; r < 3; r++) level[r] = bot[r] + 1 - (uint8_t)(fr[r] * (bot[r] - top[r] + 1) + 0.5f);
  uint8_t sunDim = fr[3] < 0.2f ? 3 : fr[3] < 0.4f ? 2 : fr[3] < 0.6f ? 1 : 0;
  if (full) display.clearScreen();


  uint8_t row[SCENE_W];
  display.setX(SCENE_X, SCENE_X + SCENE_W - 1);
  display.setY(0, SCENE_H - 1);
  display.startData();
  for (uint8_t y = 0; y < SCENE_H; y++) {
    for (uint8_t x = 0; x < SCENE_W; x++) {
      uint16_t i = (uint16_t)y * SCENE_W + x;
      uint8_t c = SCENE_IMG[i], r = SCENE_REG[i];
      if (r == 3) { for (uint8_t d = 0; d < sunDim; d++) c = dimColor(c); }
      else if (r && y < level[r]) c = dimColor(dimColor(c));
      row[x] = c;
    }
    display.writeBuffer(row, SCENE_W);
  }
  display.endTransfer();


  display.drawRect(37, 1, 22, 5, TSRectangleFilled, RGB8(0, 0, 1));
  drawTinyText(48 - tinyTextWidth("BUDDY") / 2, 1, "BUDDY", RGB8(1, 6, 4));


  const char *label = (mood == MOOD_HAPPY) ? "OK" : MOOD_NAMES[mood];
  uint8_t color = (mood == MOOD_HAPPY) ? RGB8(1, 6, 4) : (mood == MOOD_SLEEPY ? RGB8(2, 3, 5) : moodColor(mood));

  display.drawRect(14, 55, 67, 5, TSRectangleFilled, RGB8(0, 0, 1));
  drawTinyText(47 - tinyTextWidth(label) / 2, 55, label, color);
}


const uint8_t HEAD_X = 30, HEAD_Y = 18, HEAD_W = 36, HEAD_H = 28;

void drawMiniGauges() {
  float fr[3]  = { moistureFrac(), tempFrac(), lightFrac() };
  bool  ok[3]  = { moisture >= MOISTURE_THIRSTY && moisture <= MOISTURE_SOGGY,
                   temperature >= TEMP_COLD && temperature <= TEMP_HOT,
                   lightLevel >= LIGHT_DARK && lightLevel <= LIGHT_BRIGHT };

  for (uint8_t i = 0; i < 3; i++) {
    uint8_t y = 12 + i * 17;
    uint8_t bad = C_BAD;
    if (i == 0 && moisture > MOISTURE_SOGGY)    bad = moodColor(MOOD_SOGGY);
    if (i == 1 && temperature > TEMP_HOT)        bad = moodColor(MOOD_HOT);
    if (i == 2 && lightLevel > LIGHT_BRIGHT)     bad = moodColor(MOOD_BRIGHT);
    drawTinyText(3, y + 4, i == 0 ? "S" : i == 1 ? "T" : "L", ok[i] ? C_TEXT : bad);
    drawVBar(19, y, 4, SPRITE_SIZE, fr[i], ok[i] ? C_OK : bad);
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
    case MOOD_THIRSTY:
      display.drawRect(39, 28, 4, 3, TSRectangleFilled, C_FACE);
      display.drawRect(54, 28, 4, 3, TSRectangleFilled, C_FACE);
      display.drawLine(37, 26, 42, 24, C_FACE);
      display.drawLine(54, 24, 59, 26, C_FACE);
      break;
    case MOOD_COLD:
      display.drawLine(39, 25, 42, 27, C_FACE);
      display.drawLine(42, 27, 39, 29, C_FACE);
      display.drawLine(57, 25, 54, 27, C_FACE);
      display.drawLine(54, 27, 57, 29, C_FACE);
      break;
    case MOOD_SLEEPY:
      display.drawLine(39, 29, 42, 29, C_FACE);
      display.drawPixel(38, 28, C_FACE);
      display.drawPixel(43, 28, C_FACE);
      display.drawLine(54, 29, 57, 29, C_FACE);
      display.drawPixel(53, 28, C_FACE);
      display.drawPixel(58, 28, C_FACE);
      break;
    case MOOD_SOGGY:
      display.drawRect(38, 24, 6, 6, TSRectangleFilled, C_TEXT);
      display.drawRect(53, 24, 6, 6, TSRectangleFilled, C_TEXT);
      display.drawRect(40, 27, 2, 3, TSRectangleFilled, C_FACE);
      display.drawRect(55, 27, 2, 3, TSRectangleFilled, C_FACE);
      break;
    case MOOD_HOT:
      // Woozy overheated eyes: spiral-ish swirls (drawn as nested boxes).
      display.drawRect(38, 25, 5, 5, TSRectangleNoFill, C_FACE);
      display.drawPixel(40, 27, C_FACE);
      display.drawRect(54, 25, 5, 5, TSRectangleNoFill, C_FACE);
      display.drawPixel(56, 27, C_FACE);
      break;
    case MOOD_BRIGHT:
      // Squinting against the glare: tight horizontal slits with lashes.
      display.drawLine(38, 27, 44, 27, C_FACE);
      display.drawLine(38, 28, 44, 28, C_FACE);
      display.drawLine(53, 27, 59, 27, C_FACE);
      display.drawLine(53, 28, 59, 28, C_FACE);
      display.drawPixel(40, 25, C_FACE);
      display.drawPixel(57, 25, C_FACE);
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
    case MOOD_COLD: {
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
    case MOOD_SOGGY:
      display.drawRect(45, 35, 7, 5, TSRectangleFilled, C_FACE);
      break;
    case MOOD_HOT:
      // Open panting mouth with the tongue hanging out.
      display.drawRect(44, 35, 8, 4, TSRectangleFilled, C_FACE);
      display.drawRect(47, 38, 3, 3, TSRectangleFilled, C_BAD);
      break;
    case MOOD_BRIGHT:
      // Grimace: flat line with a slight downturn at the edges.
      display.drawLine(43, 37, 53, 37, C_FACE);
      display.drawPixel(43, 38, C_FACE);
      display.drawPixel(53, 38, C_FACE);
      break;
    default: break;
  }
}

void drawLeaves() {
  display.clearWindow(36, 10, 26, 8);
  uint8_t leaf = (mood == MOOD_SOGGY) ? C_LEAF_ROT : C_LEAF;
  display.drawLine(48, 11, 48, 17, leaf);
  if (mood == MOOD_THIRSTY || mood == MOOD_SOGGY || mood == MOOD_HOT) {
    // Drooping leaves when stressed by dryness, rot, or heat.
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

  display.drawRect(HEAD_X, HEAD_Y, HEAD_W, HEAD_H, TSRectangleFilled, hc);
  display.drawPixel(HEAD_X, HEAD_Y, C_BG);
  display.drawPixel(HEAD_X + HEAD_W - 1, HEAD_Y, C_BG);
  display.drawPixel(HEAD_X, HEAD_Y + HEAD_H - 1, C_BG);
  display.drawPixel(HEAD_X + HEAD_W - 1, HEAD_Y + HEAD_H - 1, C_BG);

  if (mood == MOOD_HAPPY) {
    display.drawRect(34, 33, 3, 2, TSRectangleFilled, C_CHEEK);
    display.drawRect(59, 33, 3, 2, TSRectangleFilled, C_CHEEK);
  }
  if (mood == MOOD_HOT) {
    // Flushed cheeks when overheating.
    display.drawRect(34, 33, 3, 2, TSRectangleFilled, C_BAD);
    display.drawRect(59, 33, 3, 2, TSRectangleFilled, C_BAD);
  }

  display.drawRect(32, 47, 32, 4, TSRectangleFilled, C_POT_RIM);
  for (uint8_t i = 0; i < 10; i++)
    display.drawLine(34 + i / 3, 51 + i, 61 - i / 3, 51 + i, C_POT);

  if (mood == MOOD_SOGGY) display.drawRect(24, 61, 48, 3, TSRectangleFilled, C_WATER);
}


void drawEffects(uint8_t frame) {
  switch (mood) {
    case MOOD_HAPPY: {
      bool shouldBlink = (frame % 14) == 0;
      if (shouldBlink != blinking) { blinking = shouldBlink; drawEyes(frame); }
      break;
    }
    case MOOD_THIRSTY: {
      display.clearWindow(67, 16, 6, 32);
      uint8_t y = 18 + (frame % 6) * 4;
      display.drawPixel(69, y, C_WATER);
      display.drawRect(68, y + 1, 3, 3, TSRectangleFilled, C_WATER);
      break;
    }
    case MOOD_COLD: {
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
    case MOOD_SLEEPY: {
      display.clearWindow(68, 11, 28, 34);
      display.fontColor(C_MUTED, C_BG);
      uint8_t step = frame % 4;
      display.setCursor(70, 34 - step * 3);  display.print("z");
      if (step >= 1) { display.setCursor(77, 28 - step * 3); display.print("z"); }
      if (step >= 2) { display.fontColor(C_TEXT, C_BG); display.setCursor(84, 22 - step * 3); display.print("Z"); }
      break;
    }
    case MOOD_SOGGY: {
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
    case MOOD_HOT: {
      // Sweat beads flinging off, plus wavy heat lines rising on both sides.
      display.clearWindow(25, 14, 6, 40);
      display.clearWindow(66, 14, 6, 40);
      uint8_t s = frame % 5;
      display.drawRect(27, 20 + s * 5, 2, 3, TSRectangleFilled, C_WATER);  // sweat drop
      for (uint8_t i = 0; i < 3; i++) {
        uint8_t hy = 18 + i * 10;
        uint8_t off = (frame & 1) ? 0 : 1;
        display.drawLine(68 + off, hy, 70 - off, hy + 3, C_SUN);
        display.drawLine(70 - off, hy + 3, 68 + off, hy + 6, C_SUN);
      }
      break;
    }
    case MOOD_BRIGHT: {
      // Harsh sun rays blinking in the top corners.
      display.clearWindow(66, 11, 30, 20);
      if (frame & 1) {
        uint8_t cx = 84, cy = 18;
        display.drawRect(cx - 2, cy - 2, 5, 5, TSRectangleFilled, C_SUN);
        for (uint8_t a = 0; a < 8; a++) {
          int dx = (a == 2 || a == 6) ? 0 : (a < 2 || a > 6 ? 1 : -1);
          int dy = (a == 0 || a == 4) ? 0 : (a > 0 && a < 4 ? 1 : -1);
          display.drawLine(cx + dx * 4, cy + dy * 4, cx + dx * 6, cy + dy * 6, C_SUN);
        }
      }
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

  // SOIL
  snprintf(buf, sizeof(buf), "%d %%", (int)moisture);
  bool wet = moisture > MOISTURE_SOGGY;
  bool soilOk = moisture >= MOISTURE_THIRSTY && !wet;
  drawVitalRow(12, "SOIL", buf, soilOk, wet ? "WET" : "DRY",
               moistureFrac(), MOISTURE_THIRSTY / 100.0f, MOISTURE_SOGGY / 100.0f,
               wet ? moodColor(MOOD_SOGGY) : C_BAD);

  // TEMP (now two-sided: COLD below 13, HOT above 30)
  snprintf(buf, sizeof(buf), "%d.%d C", (int)temperature, (int)(temperature * 10) % 10);
  bool hot = temperature > TEMP_HOT;
  bool tempOk = temperature >= TEMP_COLD && !hot;
  drawVitalRow(29, "TEMP", buf, tempOk, hot ? "HOT" : "COLD",
               tempFrac(), TEMP_COLD / 40.0f, TEMP_HOT / 40.0f,
               hot ? moodColor(MOOD_HOT) : moodColor(MOOD_COLD));

  // LUX (now two-sided: DARK below 1000, BRIGHT above 20000)
  snprintf(buf, sizeof(buf), "%d lx", lightLevel);
  bool bright = lightLevel > LIGHT_BRIGHT;
  bool lightOk = lightLevel >= LIGHT_DARK && !bright;
  drawVitalRow(46, "LUX", buf, lightOk, bright ? "BRGT" : "DARK",
               lightFrac(),
               log10(LIGHT_DARK + 1.0f) / log10((float)LIGHT_BRIGHT + 1.0f),
               1.0f,
               bright ? moodColor(MOOD_BRIGHT) : C_MUTED);
}


const uint8_t G_X = 14, G_Y = 12, G_W = 80, G_H = 38;

struct GraphInfo {
  const char *title;
  const char *topLabel;
  const char *botLabel;
  uint8_t     lineColor;
  float       threshold;
  float       upper;
};

const GraphInfo GRAPHS[MET_COUNT] = {
  { "SOIL",  "100", "0",  C_WATER,        MOISTURE_THIRSTY,  MOISTURE_SOGGY },
  { "TEMP",  "40",  "0",  RGB8(3, 3, 0),  TEMP_COLD,         TEMP_HOT },
  { "LIGHT", "20k", "1",  TS_8b_Yellow,   (float)LIGHT_DARK, (float)LIGHT_BRIGHT },
};


float graphFrac(uint8_t metric, float v) {
  float f;
  if (metric == MET_SOIL)      f = v / 100.0f;
  else if (metric == MET_TEMP) f = v / 40.0f;
  else                         f = log10(v + 1.0f) / log10((float)LIGHT_BRIGHT + 1.0f);
  return constrain(f, 0.0f, 1.0f);
}

uint8_t graphY(uint8_t metric, float v) {
  return G_Y + G_H - 1 - (uint8_t)(graphFrac(metric, v) * (G_H - 1));
}


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
    bool hot = temperature > TEMP_HOT;
    bool ok = temperature >= TEMP_COLD && !hot;
    snprintf(buf, sizeof(buf), "%d.%d C  %s", (int)temperature, (int)(temperature * 10) % 10,
             ok ? "OK" : (hot ? "HOT" : "COLD"));
    color = ok ? C_OK : (hot ? moodColor(MOOD_HOT) : moodColor(MOOD_COLD));
  } else {
    bool bright = lightLevel > LIGHT_BRIGHT;
    bool ok = lightLevel >= LIGHT_DARK && !bright;
    snprintf(buf, sizeof(buf), "%d lx %s", lightLevel, ok ? "OK" : (bright ? "BRGT" : "DARK"));
    color = ok ? C_OK : (bright ? moodColor(MOOD_BRIGHT) : C_BAD);
  }
  bool ok = metric == MET_SOIL ? (moisture >= MOISTURE_THIRSTY && moisture <= MOISTURE_SOGGY)
          : metric == MET_TEMP ? (temperature >= TEMP_COLD && temperature <= TEMP_HOT)
          : (lightLevel >= LIGHT_DARK && lightLevel <= LIGHT_BRIGHT);
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


  display.clearWindow(0, G_Y - 1, G_X - 1, G_H + 2);
  display.fontColor(C_MUTED, C_BG);
  display.setCursor(0, G_Y - 1);
  display.print(g.topLabel);
  display.setCursor(0, G_Y + G_H - 9);
  display.print(g.botLabel);


  display.clearWindow(G_X, G_Y, G_W, G_H);
  display.drawLine(G_X - 1, G_Y, G_X - 1, G_Y + G_H - 1, C_TRACK);
  uint8_t ty = graphY(metric, g.threshold);
  for (uint8_t x = G_X; x < G_X + G_W; x += 3) display.drawPixel(x, ty, C_BAD);
  if (g.upper >= 0) {
    uint8_t uy = graphY(metric, g.upper);
    uint8_t upperCol = metric == MET_TEMP ? moodColor(MOOD_HOT)
                     : metric == MET_LIGHT ? moodColor(MOOD_BRIGHT)
                     : moodColor(MOOD_SOGGY);
    for (uint8_t x = G_X; x < G_X + G_W; x += 4) display.drawLine(x, uy, x + 1, uy, upperCol);
  }


  for (uint8_t i = 1; i < historyCount; i++) {
    uint8_t x0 = G_X + (i - 1) * 2, x1 = G_X + i * 2;
    uint8_t overCol = metric == MET_TEMP ? moodColor(MOOD_HOT)
                    : metric == MET_LIGHT ? moodColor(MOOD_BRIGHT)
                    : moodColor(MOOD_SOGGY);
    uint8_t c = data[i] < g.threshold ? C_BAD
              : (g.upper >= 0 && data[i] > g.upper) ? overCol
              : g.lineColor;
    display.drawLine(x0, graphY(metric, data[i - 1]), x1, graphY(metric, data[i]), c);
  }

  drawGraphCaption(metric);
}


void redrawView(bool full) {
  switch (view) {
    case VIEW_BUDDY: drawBuddyView(full); break;
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

void takeReading() {
#if !USE_SIMULATED_DATA
  // Poll the real Wirelings (unless a preset is being demoed).
  if (simMode == 0) refreshLive();
#endif

  moisture    = readSoilMoisture();
  temperature = readTemperature();
  lightLevel  = readLightLevel();
  pushHistory(moisture, temperature, lightLevel);

  Mood newMood = computeMood(moisture, temperature, lightLevel);
  bool moodChanged = newMood != mood;
  mood = newMood;
  if (moodChanged) wake();

  // Buzzer: short chirp when the soil is dry (THIRSTY) on real readings.
  if (BUZZER_ON_DRY && simMode == 0 && mood == MOOD_THIRSTY) {
    tone(BUZZER_PIN, BUZZER_FREQ, 120);   // 120 ms, non-blocking
  }

  // CSV log: a heartbeat counter (proves the loop is alive even if sensors
  // fail), then moisture%, tempC, lux, mood, and per-sensor OK.
  static unsigned long readingSeq = 0;
  SerialUSB.print(readingSeq++);     SerialUSB.print(',');
  SerialUSB.print(moisture);         SerialUSB.print(',');
  SerialUSB.print(temperature);      SerialUSB.print(',');
  SerialUSB.print(lightLevel);       SerialUSB.print(',');
  SerialUSB.print(MOOD_NAMES[mood]);
#if !USE_SIMULATED_DATA
  if (simMode == 0) {
    SerialUSB.print(F(",M")); SerialUSB.print(liveMoistureOk ? F("ok") : F("ERR"));
    SerialUSB.print(F(",T")); SerialUSB.print(liveTempOk     ? F("ok") : F("ERR"));
    SerialUSB.print(F(",L")); SerialUSB.print(liveLightOk    ? F("ok") : F("ERR"));
  }
#endif
  SerialUSB.println();

  redrawView(moodChanged && view == VIEW_PET);

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
    // Cycle: LIVE, then one preset per mood (each shows its emoticon).
    simMode = (simMode + 1) % (MOOD_COUNT + 1);
    takeReading();
    redrawView(true);
    lastSensorMs = millis();
  }
  if (pressed & TSButtonUpperRight) { paused = !paused; redrawView(false); }
}


void setup() {
  // --- 1) I2C MUST come up before the display -----------------------------
  // The TinyScreen+ display driver talks over I2C, so Wire.begin() has to run
  // BEFORE display.begin() (this matches the working original sketch). Getting
  // this order wrong leaves the screen black.
  Wire.begin();

  // --- 2) Bring the SCREEN up, before any sensor work that could hang -----
  display.begin();
  display.setBrightness(BRIGHT_NORMAL);
  display.setFont(thinPixel7_10ptFontInfo);
  display.clearScreen();

  // Visible boot splash so a blank screen can't be mistaken for "dead".
  display.fontColor(TS_8b_White, C_BG);
  display.setCursor(6, 20);
  display.print("PlantGuardian");
  display.setCursor(30, 34);
  display.print("Pro");
  display.fontColor(C_MUTED, C_BG);
  display.setCursor(18, 48);
  display.print("booting...");

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // --- 3) Serial (non-blocking) -------------------------------------------
  // No wait-for-monitor loop and NO blocking sensor diagnostics here: those
  // were what froze the screen at "booting". The Wireling mux is set up once
  // without the long reset delay, and all reads are defensive, so a dead or
  // missing sensor can never hang setup() or the loop.
  SerialUSB.begin(9600);

#if !USE_SIMULATED_DATA
  Wireling.begin();            // bring up the mux
  // Configure the light sensor (quick, no long delays). The temperature and
  // moisture sensors need no config writes, and we skip the moisture RESET +
  // 1 s delay that was part of the boot hang.
  Wireling.selectPort(LIGHT_PORT);
  tsl2572Begin();
  SerialUSB.println(F("PlantGuardianPro LIVE - seq,moist%,tempC,lux,MOOD,M?,T?,L?"));
#endif

  // --- 4) Go straight to the live view ------------------------------------
  takeReading();
  redrawView(true);
  lastSensorMs = lastAnimMs = lastActivityMs = millis();
}


void loop() {
  unsigned long now = millis();

  handleButtons();

  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;
    takeReading();
  }

  if (now - lastAnimMs >= ANIM_INTERVAL_MS) {
    lastAnimMs = now;
    animFrame++;
    if (view == VIEW_PET) drawEffects(animFrame);
  }


  if (!dimmed && now - lastActivityMs > DIM_AFTER_MS) {
    display.setBrightness(BRIGHT_DIM);
    dimmed = true;
  }

  delay(10);
}

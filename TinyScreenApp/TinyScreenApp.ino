#include <Wire.h>
#include <SPI.h>
#include <TinyScreen.h>

TinyScreen display = TinyScreen(TinyScreenPlus);

void setup() {
  Wire.begin();
  SerialUSB.begin(9600);

  display.begin();
  display.setBrightness(10);
  display.setFont(thinPixel7_10ptFontInfo);
  display.fontColor(TS_8b_White, TS_8b_Black);
  display.clearScreen();
  display.setCursor(10, 10);
  display.print("Hello, INF1011!");
}

void loop() {
  static unsigned long count = 0;
  display.setCursor(10, 30);
  display.print("Count: ");
  display.print(count++);
  SerialUSB.println(count);
  delay(1000);
}

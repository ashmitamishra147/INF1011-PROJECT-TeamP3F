/*
 * PlantTypes.h
 *
 * Type definitions for the plant-monitor scenes.
 *
 * These live in a header (rather than the .ino) on purpose: the Arduino IDE
 * auto-generates function prototypes and inserts them near the top of the
 * sketch, *above* any types defined in the .ino. A function like
 * drawScene(const Scene&) would then reference `Scene` before it is declared,
 * causing a compile error such as:
 *   "'Scene' does not name a type"  /  "'Scene' was not declared in this scope"
 * Putting the types in an included header guarantees they are visible first.
 */

#ifndef PLANT_TYPES_H
#define PLANT_TYPES_H

enum Light { LIGHT_HOT, LIGHT_NORMAL, LIGHT_COLD_DARK };
enum Temp  { TEMP_HOT, TEMP_NORMAL, TEMP_COLD };
enum Soil  { SOIL_OVERWET, SOIL_JUST, SOIL_DRY };

struct Scene {
  Light light;
  Temp  temp;
  Soil  soil;
  const char* line1;   // short label, top of screen
  const char* line2;   // short label, second line
};

#endif  // PLANT_TYPES_H

# Soil Moisture Sensor (AST1037) — what it senses, plant needs, and testing

Notes compiled for the PlantDisplay project. Sources are linked inline.
Content from external sources was rephrased for compliance with licensing
restrictions.

## 1. What the sensor actually is

The TinyCircuits Soil Moisture Wireling (AST1037) is a **capacitive** sensor
built around an ATtiny microcontroller that talks over **I²C** (default address
`0x20`). It is a repackaged version of the open-source **Chirp /
i2c-moisture-sensor** design, so the Chirp protocol and behaviour apply
directly.

Sources:
- [TinyCircuits — Soil Moisture Sensor Wireling](https://tinycircuits.com/products/moisture-sensor-wireling)
  (ATtiny, capacitive sensing of dielectric permittivity, conformal-coated board)
- [Chirp i2c-moisture-sensor protocol](https://github.com/Miceuz/i2c-moisture-sensor)

### What it can measure
The firmware exposes **three** things over I²C, not just moisture:

| Quantity    | Register | Bytes | Notes                                             |
|-------------|----------|-------|---------------------------------------------------|
| Moisture    | `0x00`   | 2     | Relative capacitance; higher = wetter             |
| Light       | `0x04`   | 2     | Relative; write `0x03` first, then wait, then read. Lower = brighter. Noisy. |
| Temperature | `0x05`   | 2     | Degrees Celsius × 10 (e.g. `252` = 25.2 °C)       |
| Busy flag   | `0x09`   | 1     | `1` while a measurement is running, else `0`      |
| Reset       | `0x06`   | write | Soft reset                                        |

So the moisture Wireling can double as a rough temperature source and a
(noisy) light source. In this project the dedicated PCT2075 (temp) and TSL2572
(light) Wirelings are the primary sensors, but the Chirp's own temp channel is
a handy cross-check.

### How capacitive sensing works
Water has a very high dielectric constant (~80) compared with air (~1) and dry
soil minerals (~3–7). As soil holds more water, its overall dielectric constant
rises, which changes the capacitance the sensor measures. The sensor reports
that capacitance as a raw number.
([IERE — how a soil moisture meter works](https://iere.org/how-does-soil-moisture-meter-work/))

### Key limitations (important for interpreting readings)
- **Relative, not calibrated.** Readings are raw counts, not volumetric water
  content (VWC) or SI units. You must calibrate against your own soil/pot.
  ([Chirp README](https://github.com/Miceuz/i2c-moisture-sensor))
- **Factory baseline:** these sensors read roughly **290–310 in free air** at
  5 V. That is the practical "dry" floor. (Chirp README.)
- **Temperature and salinity affect capacitance.** Peer-reviewed studies show
  soil temperature and salinity shift capacitance readings independently of
  actual water content, so readings drift with conditions.
  ([USDA ARS study](http://www.ars.usda.gov/arsuserfiles/20361500/pdf_pubs/P1878.pdf),
  [MDPI Sensors — temperature correction](https://www.mdpi.com/1424-8220/12/7/9773/pdf))
- **Sensing volume is local.** It measures the soil immediately around the
  probe, so probe placement and depth matter.
- **Do not submerge the connector.** The board is conformal-coated for splash
  resistance but the 5-pin connector is not. (TinyCircuits product page.)

## 2. Linking readings to plant watering needs

### The horticultural target
Agronomy references express plant water needs as **volumetric water content
(VWC)**. For most common houseplants during active growth, the healthy band is
roughly **35–55% VWC**, and research shows watering to a sensor target (around
35–45% VWC) outperforms fixed calendar schedules.
([Cornell/UFlorida summaries via lifetips](https://lifetips.alibaba.com/plant-care/plant-moisture-level-chart),
[science-based watering guide](https://lifetips.alibaba.com/plant-care/regar-planta))

Two practical takeaways from the literature:
- **Overwatering is the bigger killer.** Persistent saturation causes root
  suffocation and fungal rot within days, so an "overwet" warning is as useful
  as a "dry" one.
- **Water needs are species- and soil-specific.** A ZZ plant can go weeks; a
  spider plant needs water every few days in the same spot. One fixed threshold
  will not fit every plant.
  ([UFlorida IFAS via plant-water-needs-chart](https://lifetips.alibaba.com/plant-care/plant-water-needs-chart))

### Mapping the raw sensor value to a usable scale
Because the sensor is relative, the honest approach is a **two-point
calibration** that turns raw counts into a 0–100% "relative moisture" scale —
not true VWC, but repeatable for a given plant/soil:

1. **0% reference (dry):** reading with the probe in dry air (or bone-dry soil).
   Expect something near the 290–310 free-air baseline.
2. **100% reference (wet):** reading with the probe in water (to just below the
   chip) or in freshly saturated, drained soil.
3. Map live readings linearly between those two points:

   ```
   percent = (raw - dryRef) * 100 / (wetRef - dryRef)   // clamped 0..100
   ```

This is exactly what `Sensors.h` does with `moistureDry` / `moistureWet`.

### Suggested watering bands (tune per plant)
Once you have a 0–100% relative scale, the project maps it to three states in
`PlantDisplay.ino`. Reasonable starting thresholds for a typical potting mix:

| Relative moisture | State       | Meaning / action                         |
|-------------------|-------------|------------------------------------------|
| `>= 70%`          | OVERWET     | Too wet — let it dry, risk of root rot   |
| `30% – 70%`       | SOIL OK     | In the healthy band, no action           |
| `<= 30%`          | NEEDS WATER | Dry — water it (buzzer chirps)           |

These correspond to `SOIL_WET_PCT`, `SOIL_DRY_PCT` and `DRY_ALERT_PCT` in the
sketch. For a succulent/cactus, lower the whole band (water only well below
20%); for a moisture-loving plant (ferns, peace lily), raise the dry threshold.

### Why also read temperature
Capacitance drifts with temperature, and the plant's water demand itself rises
with warmth and light (higher transpiration). Showing temperature alongside
moisture — which this project already does — gives context: a "NEEDS WATER"
reading in a hot, bright spot is more urgent than the same reading in a cool,
dark one.

## 3. Testing for basic functionality

Work from "is it even talking" up to "does the number track reality."

### Test 0 — I²C presence (does the sensor respond at all)
Select the moisture port on the adapter, then do an I²C address scan. The
sensor should acknowledge at `0x20`. If nothing shows up:
- check the 5-pin cable seating and that it is on the port you configured
  (`MOISTURE_PORT`),
- confirm `Wireling.selectPort(MOISTURE_PORT)` runs before the scan.

A minimal scanner sketch is in `tests/MoistureSensorTest.ino` (below) — it
prints every address that answers on the selected port.

### Test 1 — raw value sanity (free air)
With the probe clean, dry, and held **by the cable only** (your hand changes
the reading), open the Serial monitor at 115200. You should see a stable raw
value in the low hundreds (~290–310 per the Chirp baseline). A value stuck at
`0` or `65535` usually means a wiring/port problem, not a dry reading.

### Test 2 — responds to water (the core functional test)
1. Note the dry raw value.
2. Dip the probe in a glass of water to **just below the black chip** (never let
   water reach the connector).
3. The raw value should jump up substantially and settle.
4. Pull it out and dry it; the value should fall back toward the dry baseline.

If the number moves clearly up in water and back down when dry, the sensor is
functional. The size of that swing (dry → wet) is exactly the range you plug
into `moistureDry` / `moistureWet`.

### Test 3 — touch test (quick smoke test)
Pinching the sensor pads/body with wet fingers nudges the reading up. Good for a
10-second "is it alive" check, but use the water dip for real calibration.

### Test 4 — temperature cross-check (optional)
Read register `0x05` (value is °C × 10) and compare it to the PCT2075 reading on
the temperature Wireling. They should agree within a couple of degrees. A large
mismatch points at a bad sensor or a wiring issue.

### Gotcha: the "previous reading" behaviour
The Chirp firmware returns the value from the **previous** read and then starts
a new measurement. For instant, accurate single reads, **read twice** (discard
the first) or poll the busy register `0x09` until it returns `0`. The test
sketch below reads twice so the displayed value is current.
([Chirp README — measurement timing](https://github.com/Miceuz/i2c-moisture-sensor))

## 4. Calibration procedure (turning tests into thresholds)

1. Run the test sketch, Serial monitor open.
2. Record `rawDry` (probe in dry air, held by cable).
3. Record `rawWet` (probe in water to just below the chip).
4. Put those numbers into `moistureDry` and `moistureWet` in `Sensors.h`.
5. Re-upload. The on-screen % should now read ~0% in dry air and ~100% in water.
6. Optionally insert the probe in freshly watered, drained soil to see where
   "field capacity" lands on your scale, and set `SOIL_WET_PCT` just below it.

After calibration the 0–100% number is repeatable for that plant/soil
combination, which is all you need to drive the watering states and buzzer.

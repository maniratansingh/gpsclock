# GPS Clock (gpsclock)

A high-precision, dual-display GPS Clock built for Arduino. This project combines a MAX7219 LED Matrix for a large, bright time display and an SSD1306 OLED screen for an advanced dashboard showing real-time GPS telemetry.

## Pro Architecture Features

- **Silent Window I2C Synchronization:** The OLED display updates are perfectly synchronized to the physical pauses in the GPS NMEA data stream. This prevents the tiny 64-byte Arduino serial buffer from overflowing while the I2C bus is occupied, ensuring zero dropped sentences.
- **Split-Tier Navigation Logic (2D/3D):** Architected specifically for weak or small ceramic antennas. Latitude, Longitude, and Speed update aggressively on a minimum 3-satellite (2D) fix, while Altitude remains strictly guarded by a 4-satellite (3D) requirement to prevent vertical drift.
- **Persistent Location Memory:** If the GPS signal is completely lost (e.g., walking indoors), the OLED elegantly freezes on your last known good location instead of displaying zeroes or garbage coordinates.
- **Null Island Defense:** Automatically rejects floating-point `0.0000` coordinate streams caused by hardware glitches.
- **Advanced Display Caching:** The MAX7219 matrix caches its active digits in memory (`lastMaxDigits`). It only transmits SPI packets for the specific digits that have physically changed on the clock, resulting in lightning-fast redraws.
- **SRAM Optimized:** Eliminates all bulky character arrays. Telemetry floats are printed directly to the OLED to maximize free memory on the Arduino Nano.
- **Robust State Management:** Global variables are neatly organized into isolated `GPSTelemetry`, `ClockState`, and `DisplayState` structures.
- **Anti-Freeze Fallback:** If you drive under a bridge or lose GPS signal, the Arduino's internal crystal oscillator seamlessly takes over to keep the clock ticking forward without freezing.
- **Dual Display Sync:** The time and blinking colon are perfectly phase-locked between the MAX7219 matrix and the OLED dashboard.

## Hardware Required

- 1x Arduino (Nano / Uno)
- 1x Neo-6M GPS Module (with ceramic antenna)
- 1x MAX7219 8x8 LED Matrix (or 4-in-1 matrix block)
- 1x SSD1306 OLED Display (128x32, I2C)

## Comprehensive Wiring & Diagram Reference

### Visual Wiring Diagram
```text
                          +-------------------+
                          |                   |
                          |   ARDUINO NANO    |
                          |                   |
               +--------->| 5V                |
               | +------->| GND               |
               | |        |                   |
               | |        | D3 (RX) <------------- TX (GPS Neo-6M)
               | |        | D4 (TX) -------------> RX (GPS Neo-6M)
               | |        |                   |
               | |        | A4 (SDA) <-----------> SDA (SSD1306 OLED)
               | |        | A5 (SCL) <-----------> SCL (SSD1306 OLED)
               | |        |                   |
               | |        | D5 (MOSI) -----------> DIN (MAX7219)
               | |        | D6 (SS)   -----------> CS  (MAX7219)
               | |        | D7 (SCK)  -----------> CLK (MAX7219)
               | |        +-------------------+
               | |
+---------+    | |    +---------+       +---------+
| Neo-6M  |    | |    | SSD1306 |       | MAX7219 |
|   GPS   |    | |    |  OLED   |       | Matrix  |
+---------+    | |    +---------+       +---------+
| VCC     |----+ |    | VCC     |----+  | VCC     |----+
| GND     |------+    | GND     |----+  | GND     |----+
| TX      |           | SCL     |       | DIN     |
| RX      |           | SDA     |       | CS      |
+---------+           +---------+       | CLK     |
                                        +---------+
```

### Complete Pinout Table

| Component | Component Pin | Arduino Pin | Notes / Description |
| :--- | :--- | :--- | :--- |
| **Neo-6M GPS** | VCC | 5V | GPS requires steady power; 5V is recommended for most Neo-6M breakout boards. |
| | GND | GND | Common ground. |
| | TX | D3 | Transmits NMEA sentences to the Arduino's SoftwareSerial RX pin. |
| | RX | D4 | (Optional) Receives commands from Arduino's SoftwareSerial TX pin. |
| **SSD1306 OLED** | VCC | 5V / 3.3V | I2C Display power. |
| | GND | GND | Common ground. |
| | SCL | A5 | I2C Clock Line. |
| | SDA | A4 | I2C Data Line. |
| **MAX7219 Matrix** | VCC | 5V | **MUST be 5V.** LED matrices draw significant current. |
| | GND | GND | Common ground. |
| | DIN | D5 | SPI Data In (MOSI). |
| | CS | D6 | SPI Chip Select (SS). |
| | CLK | D7 | SPI Clock (SCK). |

### Important Power Warning ⚠️
If you are running the MAX7219 matrix, the OLED, and the GPS module all at the same time, they can draw a significant amount of current (especially the LED matrix when many LEDs are lit). If you experience brown-outs, random freezing, or the GPS failing to get a fix, **do not power this purely from a cheap laptop USB port**. Provide a strong 5V power supply (like a wall charger) directly to the Arduino.

## First Run & Cold Starts

GPS modules require a **Cold Start** to download Ephemeris data before they can calculate a location. 
1. Take the project outside under a clear, open sky.
2. Ensure the ceramic antenna is pointing straight up.
3. Keep the antenna away from the LED screens to prevent electrical interference.
4. **Stand completely still for 2 to 5 minutes.** 

*Note: The time will appear instantly (1 satellite required), but the Latitude, Longitude, Speed, and Altitude will not appear until the module achieves a full 3D lock (3+ satellites).*

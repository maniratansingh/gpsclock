# GPS Clock (gpsclock)

A high-precision, dual-display GPS Clock built for Arduino. This project combines a MAX7219 LED Matrix for a large, bright time display and an SSD1306 OLED screen for an advanced dashboard showing real-time GPS telemetry.

## Features

- **Dual Display Sync:** The time and blinking colon are perfectly phase-locked between the MAX7219 matrix and the OLED dashboard.
- **Ultra-Fast Time Fix:** The clock extracts time from the very first satellite it sees, allowing the time to fix almost instantly.
- **Anti-Freeze Fallback:** If you drive under a bridge or lose GPS signal, the Arduino's internal crystal oscillator seamlessly takes over to keep the clock ticking forward without freezing.
- **Advanced Telemetry:** Displays Latitude, Longitude, Altitude, Speed, and Satellite Count in real-time.
- **Robust Data Validation:** Only displays valid, verified GPS location data, preventing glitchy or false coordinates from appearing on the screen.

## Hardware Required

- 1x Arduino (Nano / Uno)
- 1x Neo-6M GPS Module (with ceramic antenna)
- 1x MAX7219 8x8 LED Matrix (or 4-in-1 matrix block)
- 1x SSD1306 OLED Display (128x32, I2C)

## Wiring Guide

### Neo-6M GPS (SoftwareSerial)
- **VCC:** 5V or 3.3V (Depends on your module)
- **GND:** GND
- **TX:** Arduino Pin 3
- **RX:** Arduino Pin 4

### SSD1306 OLED (I2C)
- **VCC:** 3.3V or 5V
- **GND:** GND
- **SCL:** Arduino A5
- **SDA:** Arduino A4

### MAX7219 Matrix (SPI)
- **VCC:** 5V
- **GND:** GND
- **DIN:** Arduino Pin 5
- **CS:** Arduino Pin 6
- **CLK:** Arduino Pin 7

## First Run & Cold Starts

GPS modules require a **Cold Start** to download Ephemeris data before they can calculate a location. 
1. Take the project outside under a clear, open sky.
2. Ensure the ceramic antenna is pointing straight up.
3. Keep the antenna away from the LED screens to prevent electrical interference.
4. **Stand completely still for 2 to 5 minutes.** 

*Note: The time will appear instantly (1 satellite required), but the Latitude, Longitude, Speed, and Altitude will not appear until the module achieves a full 3D lock (3+ satellites).*

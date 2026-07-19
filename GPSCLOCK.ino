#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <LedControl.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

SoftwareSerial gpsSerial(3, 4);
TinyGPSPlus gps;
LedControl lc = LedControl(5, 7, 6, 1);

// Configuration Constants
const uint16_t OLED_REFRESH_MS = 500;
const uint16_t CLOCK_INTERVAL_MS = 1000;
const int16_t IST_OFFSET_HOURS = 5;
const int16_t IST_OFFSET_MINUTES = 30;
const uint32_t GPS_BAUD_RATE = 9600;

// Data Models
struct GPSTelemetry {
  float lat = 0.0;
  float lon = 0.0;
  float altitude = 0.0;
  float speed = 0.0;
  uint8_t satellites = 0;
  bool hasFix = false;
} telemetry;

struct ClockState {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  bool isTimeReal = false;
} clockState;

struct DisplayState {
  bool showColon = false;
  uint8_t lastMaxDigits[8] = {255, 255, 255, 255, 255, 255, 255, 255}; // 255 forces initial draw
} displayState;

struct GPSStats {
  uint32_t charsProcessed = 0;
  uint32_t sentencesParsed = 0;
  uint32_t failedChecksum = 0;
} stats;

// Timers
unsigned long lastOLEDUpdate = 0;
unsigned long lastGPSByteRcvd = 0; // Tracks when the GPS is transmitting

void initOLED();
void initGPS();
void initMAX7219();
void readGPS();
void updateClock();
void utcToIST();
bool isLeapYear(int y);
int daysInMonth(int m, int y);
void incrementDate();
void updateOLED();
void updateMAX7219();

void setup() {
  Serial.begin(115200); // Debug
  initGPS();
  initMAX7219();
  initOLED();
}

void loop() {
  readGPS();
  updateClock();
  updateMAX7219();
  
  // The OLED display.display() call takes ~50ms over I2C, which is long enough to overflow
  // the tiny 64-byte SoftwareSerial buffer if the GPS is actively transmitting a burst.
  // We MUST wait until the GPS goes completely silent (>20ms gap) before drawing to the OLED!
  if (millis() - lastOLEDUpdate >= OLED_REFRESH_MS) {
    if (millis() - lastGPSByteRcvd > 20) {
      lastOLEDUpdate = millis();
      updateOLED();
    }
  }
}

void initOLED() {
  Wire.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED init failed"));
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.display();
}

void initGPS() {
  gpsSerial.begin(GPS_BAUD_RATE);
}

void initMAX7219() {
  lc.shutdown(0, false);
  lc.setIntensity(0, 4);
  lc.clearDisplay(0);
}

void readGPS() {
  while (gpsSerial.available() > 0) {
    lastGPSByteRcvd = millis();
    gps.encode(gpsSerial.read());
  }
  
  // 30-SECOND FLYWHEEL
  // If the GPS is erratic, we hold onto the last valid location for up to 30 seconds.
  // This prevents the screen from flickering 'NO GPS' when you are outside.
  if (gps.location.isValid() && gps.location.age() < 30000) {
    telemetry.hasFix = true;
    telemetry.lat = gps.location.lat();
    telemetry.lon = gps.location.lng();
    telemetry.speed = gps.speed.kmph();
    telemetry.altitude = gps.altitude.meters();
  } else {
    // If you go indoors and 30 seconds pass with no fix, the clock officially DIES.
    telemetry.hasFix = false;
    telemetry.lat = 0.0;
    telemetry.lon = 0.0;
    telemetry.speed = 0.0;
    telemetry.altitude = 0.0;
  }
  
  if (gps.satellites.isValid() && gps.satellites.age() < 10000) {
    telemetry.satellites = gps.satellites.value();
  } else {
    telemetry.satellites = 0;
  }

  stats.charsProcessed = gps.charsProcessed();
  stats.sentencesParsed = gps.sentencesWithFix();
  stats.failedChecksum = gps.failedChecksum();
}

void updateClock() {
  clockState.isTimeReal = telemetry.hasFix;

  if (clockState.isTimeReal) {
    static unsigned long secondStartTime = 0;
    static uint8_t lastCheckedGPSSec = 255;
    
    // GPS Master Clock Synchronization
    if (gps.time.second() != lastCheckedGPSSec) {
      lastCheckedGPSSec = gps.time.second();
      
      // Instantly re-lock the internal clock
      utcToIST();
      secondStartTime = millis();
    }
    
    // Anti-Freeze Coasting
    // The internal oscillator seamlessly drives the clock smoothly between erratic GPS drops
    if (millis() - secondStartTime >= CLOCK_INTERVAL_MS) {
      secondStartTime += CLOCK_INTERVAL_MS;
      clockState.second++;
      if (clockState.second >= 60) {
        clockState.second = 0; 
        clockState.minute++;
        if (clockState.minute >= 60) { 
            clockState.minute = 0; 
            clockState.hour++; 
            if (clockState.hour >= 24) {
                clockState.hour = 0;
                incrementDate();
            }
        }
      }
    }
    
    displayState.showColon = ((millis() - secondStartTime) % 1000) < 500;
  } else {
    displayState.showColon = (millis() / 500) % 2 == 0;
  }
}

bool isLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int daysInMonth(int m, int y) {
  int days[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && isLeapYear(y)) return 29;
  return days[m];
}

void incrementDate() {
  clockState.day++;
  if (clockState.day > daysInMonth(clockState.month, clockState.year)) {
    clockState.day = 1;
    clockState.month++;
    if (clockState.month > 12) {
      clockState.month = 1;
      clockState.year++;
    }
  }
}

void utcToIST() {
  clockState.year = gps.date.year();
  clockState.month = gps.date.month();
  clockState.day = gps.date.day();
  clockState.hour = gps.time.hour();
  clockState.minute = gps.time.minute();
  clockState.second = gps.time.second();

  clockState.minute += IST_OFFSET_MINUTES;
  clockState.hour += IST_OFFSET_HOURS;

  if (clockState.minute >= 60) {
    clockState.minute -= 60;
    clockState.hour += 1;
  }
  if (clockState.hour >= 24) {
    clockState.hour -= 24;
    incrementDate();
  }
}

void updateMAX7219() {
  char sep = displayState.showColon ? '-' : ' ';
  
  if (clockState.isTimeReal) {
    uint8_t digits[8] = {
      (uint8_t)(clockState.second % 10),
      (uint8_t)(clockState.second / 10),
      (uint8_t)sep,
      (uint8_t)(clockState.minute % 10),
      (uint8_t)(clockState.minute / 10),
      (uint8_t)sep,
      (uint8_t)(clockState.hour % 10),
      (uint8_t)(clockState.hour / 10)
    };

    for (int i = 0; i < 8; i++) {
      if (displayState.lastMaxDigits[i] != digits[i]) {
        displayState.lastMaxDigits[i] = digits[i];
        if (i == 2 || i == 5) {
          lc.setChar(0, i, (char)digits[i], false);
        } else {
          lc.setDigit(0, i, digits[i], false);
        }
      }
    }
  } else {
    for (int i = 0; i < 8; i++) {
      uint8_t val = (i == 2 || i == 5) ? sep : '-';
      if (displayState.lastMaxDigits[i] != val) {
        displayState.lastMaxDigits[i] = val;
        lc.setChar(0, i, (char)val, false);
      }
    }
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setCursor(0, 0);
  
  char cSep = displayState.showColon ? ':' : ' ';
  
  if (clockState.isTimeReal) {
    display.setTextSize(1);
    char timeBuf[30];
    sprintf(timeBuf, "%02d%c%02d%c%02d FIX:OK S:%02d", clockState.hour, cSep, clockState.minute, cSep, clockState.second, telemetry.satellites);
    display.println(timeBuf);
    
    display.print(F("LAT:")); 
    display.println(telemetry.lat, 6);
    
    display.print(F("LON:")); 
    display.println(telemetry.lon, 6);
    
    display.print(F("ALT:"));
    display.print(telemetry.altitude, 1);
    display.print(F(" SPD:"));
    display.println(telemetry.speed, 1);
  } else {
    display.setTextSize(2);
    display.setCursor(0, 8);
    display.println(F(" NO GPS "));
    
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print(F("SATS: "));
    display.print(telemetry.satellites);
  }

  display.display();
}
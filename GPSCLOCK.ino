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

void initOLED();
void initGPS();
void configureGPS();
void initMAX7219();
void readGPS();
void updateClock();
void utcToIST();
bool isLeapYear(int y);
int daysInMonth(int m, int y);
void incrementDate();
void updateOLED();
void updateMAX7219();
void sendUBX(const uint8_t *msg, uint8_t len);

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
  
  if (millis() - lastOLEDUpdate >= OLED_REFRESH_MS) {
    lastOLEDUpdate = millis();
    updateOLED();
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

void sendUBX(const uint8_t *msg, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    gpsSerial.write(msg[i]);
  }
  delay(10);
}

void configureGPS() {
  // Disable GLL
  const uint8_t disableGLL[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A};
  sendUBX(disableGLL, sizeof(disableGLL));
  // Disable GSA
  const uint8_t disableGSA[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x31};
  sendUBX(disableGSA, sizeof(disableGSA));
  // Disable VTG
  const uint8_t disableVTG[] = {0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x46};
  sendUBX(disableVTG, sizeof(disableVTG));
}

void initGPS() {
  gpsSerial.begin(GPS_BAUD_RATE);
  delay(100);
  configureGPS();
}

void initMAX7219() {
  lc.shutdown(0, false);
  lc.setIntensity(0, 4);
  lc.clearDisplay(0);
}

void readGPS() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
  
  // 1. Current Live Status of the Fix (Transient)
  // Used to drive the FIX:OK / FIX:-- indicator on the OLED
  telemetry.hasFix = gps.location.isValid() && gps.location.age() < 2000 && gps.satellites.isValid() && gps.satellites.value() >= 4;
  
  // 2. Persistent Location Handling (Last Known Good)
  // We only overwrite the persistent variables when a fresh, mathematically valid, 
  // and geometrically strong (4+ sats) 3D coordinate arrives.
  if (gps.location.isUpdated() && 
      gps.location.isValid() && 
      gps.satellites.isValid() && 
      gps.satellites.value() >= 4) {
    
    // Null Island / Zero-Coordinate Defense (Reject anything < 0.001 degrees)
    if (abs(gps.location.lat()) > 0.001 || abs(gps.location.lng()) > 0.001) {
      telemetry.lat = gps.location.lat();
      telemetry.lon = gps.location.lng();
      telemetry.speed = gps.speed.kmph();
      telemetry.altitude = gps.altitude.meters();
    }
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
  clockState.isTimeReal = gps.time.isValid() && gps.date.isValid() && gps.date.year() > 2020;

  if (clockState.isTimeReal) {
    static unsigned long secondStartTime = 0;
    
    if (gps.time.isUpdated()) {
      utcToIST();
      secondStartTime = millis();
    } else {
      if (millis() - secondStartTime >= CLOCK_INTERVAL_MS) {
        secondStartTime += CLOCK_INTERVAL_MS;
        clockState.second++;
        if (clockState.second >= 60) {
          clockState.second = 0; clockState.minute++;
          if (clockState.minute >= 60) { clockState.minute = 0; clockState.hour++; if (clockState.hour >= 24) clockState.hour = 0; }
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
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  char timeBuf[30];
  char cSep = displayState.showColon ? ':' : ' ';
  if (clockState.isTimeReal) {
    sprintf(timeBuf, "%02d%c%02d%c%02d FIX:%s S:%02d", clockState.hour, cSep, clockState.minute, cSep, clockState.second, telemetry.hasFix ? "OK" : "--", telemetry.satellites);
  } else {
    sprintf(timeBuf, "--%c--%c-- FIX:%s S:%02d", cSep, cSep, telemetry.hasFix ? "OK" : "--", telemetry.satellites);
  }
  display.println(timeBuf);
  
  display.print(F("LAT:")); 
  display.println(telemetry.lat, 6);
  
  display.print(F("LON:")); 
  display.println(telemetry.lon, 6);
  
  display.print(F("ALT:"));
  display.print(telemetry.altitude, 1);
  display.print(F(" SPD:"));
  display.println(telemetry.speed, 1);

  display.display();
}
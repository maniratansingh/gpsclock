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

const int offsetHours = 5;
const int offsetMinutes = 30;

int lYear, lMonth, lDay, lHour, lMin, lSec;
float gLat = 0.0, gLon = 0.0, gSpeed = 0.0, gAlt = 0.0;
int gSats = 0;
bool hasFix = false;

unsigned long lastPageChange = 0;
int currentPage = 1;
bool globalShowColon = false;
char sharedTimeStrOLED[30];
unsigned long lastSerialPrint = 0;

void initOLED();
void initGPS();
void initMAX7219();
void readGPS();
void updateClock();
void utcToIST();
bool isLeapYear(int y);
int daysInMonth(int m, int y);
void incrementDate();
void drawClockPage();
void updateOLED();
void updateMAX7219();
void displayTime();
void displayError();
void blinkColon();
void centerText(const char* text, int y);
void clearPage();

void setup() {
  initGPS();
  initMAX7219();
  initOLED();
}

void loop() {
  readGPS();
  updateClock();

  // Only update the displays when the time changes or the colon blinks (twice a second).
  // This stops the OLED from hogging the CPU and makes the clock flipping incredibly sharp!
  static bool lastColonState = false;
  static int lastSecState = -1;
  bool gpsUpdated = gps.location.isUpdated() || gps.speed.isUpdated() || gps.satellites.isUpdated();
  
  if (globalShowColon != lastColonState || lSec != lastSecState || gpsUpdated) {
    lastColonState = globalShowColon;
    lastSecState = lSec;
    
    updateMAX7219();
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

void clearPage() {
  display.clearDisplay();
}

void centerText(const char* text, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

void initGPS() {
  gpsSerial.begin(9600);
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
  
  // Use .age() to guarantee the data is live. 5000ms allows skipping a few pings under trees without dropping.
  if (gps.location.isValid() && gps.location.age() < 5000) {
    hasFix = true;
    gLat = gps.location.lat();
    gLon = gps.location.lng();
    gSpeed = gps.speed.kmph();
    gAlt = gps.altitude.meters();
  } else {
    hasFix = false;
  }
  
  // Satellites are broadcast less frequently on some modules. Use a 10s age to prevent flickering to 0.
  if (gps.satellites.isValid() && gps.satellites.age() < 10000) {
    gSats = gps.satellites.value();
  } else {
    gSats = 0;
  }
}

void updateClock() {
  bool isTimeReal = gps.time.isValid() && gps.date.isValid() && gps.date.year() > 2020;

  if (isTimeReal) {
    static unsigned long secondStartTime = 0;
    
    // Only pull new time if GPS just parsed a new sentence
    if (gps.time.isUpdated()) {
      utcToIST();
      secondStartTime = millis();
    } else {
      // Coast on Arduino's internal clock if GPS signal drops/delays (prevents freezing)
      if (millis() - secondStartTime >= 1000) {
        secondStartTime += 1000;
        lSec++;
        if (lSec >= 60) {
          lSec = 0; lMin++;
          if (lMin >= 60) { lMin = 0; lHour++; if (lHour >= 24) lHour = 0; }
        }
      }
    }
    
    // Keep colon blinking forever, locked to the real/coasted second
    globalShowColon = ((millis() - secondStartTime) % 1000) < 500;
    
    char cSep = globalShowColon ? ':' : ' ';
    sprintf(sharedTimeStrOLED, "%02d%c%02d%c%02d FIX:%s S:%02d", lHour, cSep, lMin, cSep, lSec, hasFix ? "OK" : "--", gSats);
  } else {
    // If no real time, just blink based on the raw Arduino clock
    globalShowColon = (millis() / 500) % 2 == 0;
    char cSep = globalShowColon ? ':' : ' ';
    sprintf(sharedTimeStrOLED, "--%c--%c-- FIX:%s S:%02d", cSep, cSep, hasFix ? "OK" : "--", gSats);
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
  lDay++;
  if (lDay > daysInMonth(lMonth, lYear)) {
    lDay = 1;
    lMonth++;
    if (lMonth > 12) {
      lMonth = 1;
      lYear++;
    }
  }
}

void utcToIST() {
  lYear = gps.date.year();
  lMonth = gps.date.month();
  lDay = gps.date.day();
  lHour = gps.time.hour();
  lMin = gps.time.minute();
  lSec = gps.time.second();

  lMin += 30;
  lHour += 5;

  if (lMin >= 60) {
    lMin -= 60;
    lHour += 1;
  }
  if (lHour >= 24) {
    lHour -= 24;
    incrementDate();
  }
}

void updateOLED() {
  clearPage();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  // Line 1: Time, Fix and Satellites (Perfectly synced with MAX7219)
  display.println(sharedTimeStrOLED);
  
  // Line 2: Latitude (Always show data immediately without waiting for hasFix flag)
  char latBuf[16];
  dtostrf(gLat, 11, 6, latBuf);
  display.print(F("LAT:")); 
  display.println(latBuf);
  
  // Line 3: Longitude 
  char lonBuf[16];
  dtostrf(gLon, 11, 6, lonBuf);
  display.print(F("LON:")); 
  display.println(lonBuf);
  
  // Line 4: Altitude and Speed (1 decimal place for stable accuracy without jumping)
  char altBuf[8];
  dtostrf(gAlt, 0, 1, altBuf);
  char spdBuf[8];
  dtostrf(gSpeed, 0, 1, spdBuf);
  char altSpdBuf[28];
  sprintf(altSpdBuf, "ALT:%s SPD:%s", altBuf, spdBuf);
  display.println(altSpdBuf);

  display.display();
}

void updateMAX7219() {
  bool isTimeReal = gps.time.isValid() && gps.date.isValid() && gps.date.year() > 2020;
  if (isTimeReal) {
    displayTime();
  } else {
    displayError();
  }
}

void displayTime() {
  char sep = globalShowColon ? '-' : ' ';
  
  lc.setDigit(0, 7, lHour / 10, false);
  lc.setDigit(0, 6, lHour % 10, false);
  lc.setChar(0, 5, sep, false);
  lc.setDigit(0, 4, lMin / 10, false);
  lc.setDigit(0, 3, lMin % 10, false);
  lc.setChar(0, 2, sep, false);
  lc.setDigit(0, 1, lSec / 10, false);
  lc.setDigit(0, 0, lSec % 10, false);
}

void displayError() {
  char sep = globalShowColon ? '-' : ' ';
  
  lc.setChar(0, 7, '-', false);
  lc.setChar(0, 6, '-', false);
  lc.setChar(0, 5, sep, false);
  lc.setChar(0, 4, '-', false);
  lc.setChar(0, 3, '-', false);
  lc.setChar(0, 2, sep, false);
  lc.setChar(0, 1, '-', false);
  lc.setChar(0, 0, '-', false);
}

void blinkColon() {
  // Implementation combined in displayTime()
}
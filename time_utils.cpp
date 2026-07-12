#include "time_utils.h"
#include <WiFi.h>
// #include "time.h"
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "esp_attr.h"

const char* ntpServer = "europe.pool.ntp.org";
const long  gmtOffset_sec = 3600; // GMT+1
const int   daylightOffset_sec = 3600; // Daylight saving time offset
bool wifiWorking = false;
bool timeWorking = false;
struct tm timeinfo;

// Survive deep sleep (reset to 0/false only on power loss or cold boot).
RTC_DATA_ATTR uint32_t wakeCounter    = 0;
RTC_DATA_ATTR bool     timeSyncedOnce = false;

#define USE_MOCK_TIME 0

#if USE_MOCK_TIME
  bool getMockLocalTime(struct tm *timeinfo) {
      // Simulate obtaining time by setting a fixed time or incrementing a counter
      static time_t mockTime = 1725514140;
      *timeinfo = *localtime(&mockTime);
      return true;
  }
#endif

// Function declarations
// void initializeTime();

// Function definitions
void initializeWifi() {

    // Open setup.json file
    File file = SD.open("/setup.json");
    if (!file) {
        Serial.println("Failed to open setup.json file");
        return;
    }

    // Allocate a buffer to store contents of the file
    size_t size = file.size();
    std::unique_ptr<char[]> buf(new char[size]);

    // Read file contents into buffer
    file.readBytes(buf.get(), size);
    file.close();

    // Parse JSON
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, buf.get());
    if (error) {
        Serial.println("Failed to parse setup.json");
        return;
    }

    const char* ssid = doc["ssid"];
    const char* password = doc["password"];
    Serial.println("Connecting to WiFi: " + String(ssid));
    Serial.println("Password: " + String(password));

    // Connect to WiFi
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      attempts++;
      Serial.print(".");
      if(attempts == 10){
        Serial.println("Failed to connect to WiFi");
        return;
      }
    }
    wifiWorking = true;
    Serial.println("Connected to WiFi");
}
void initializeTime() {
    if(!wifiWorking){
      Serial.println("Failed to obtain time, no wifi connection");
      return;
    }
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Retry in case of failure in getting time
    int attempts = 0; // Reset attempts for time-sync
    while (
      #if USE_MOCK_TIME
              !getMockLocalTime(&timeinfo)
      #else
              !getLocalTime(&timeinfo)
      #endif
    ) { // Increased attempts
      Serial.println("Failed to obtain time, retrying...");
      delay(1000); // Delay before retrying
      attempts++;
      if(attempts == 10){
        Serial.println("Failed to obtain time for good");
        return;
      }
    }

    timeWorking = true;
    Serial.println("Time successfully obtained");
    Serial.println(&timeinfo, "Current time: %A, %B %d %Y %H:%M:%S");
}

bool syncTimeIfNeeded() {
    wakeCounter++;

    bool needsSync = !timeSyncedOnce || (wakeCounter % NTP_SYNC_EVERY_N_WAKES == 0);

    if (!needsSync) {
        // Most wakes: no WiFi. The ESP32 RTC clock keeps running through deep
        // sleep, so the system time set by a previous NTP sync is still valid —
        // just read it back directly.
        Serial.println("Skipping WiFi/NTP this wake (wake #" + String(wakeCounter) + ").");
        wifiWorking = false;
        timeWorking =
          #if USE_MOCK_TIME
                  getMockLocalTime(&timeinfo);
          #else
                  getLocalTime(&timeinfo, 10);
          #endif
        return timeWorking;
    }

    Serial.println("Syncing WiFi/NTP this wake (wake #" + String(wakeCounter) + ").");
    initializeWifi();
    initializeTime();
    if (timeWorking) timeSyncedOnce = true;
    return timeWorking;
}

bool isWithinQuietHours() {
#ifdef QUIET_HOURS_ENABLED
    if (!timeWorking) return false;  // Unknown time — ignore quiet hours, refresh normally
    int h = timeinfo.tm_hour;
    if (QUIET_HOURS_START == QUIET_HOURS_END) return false;  // degenerate window = disabled
    if (QUIET_HOURS_START < QUIET_HOURS_END) {
        return h >= QUIET_HOURS_START && h < QUIET_HOURS_END;
    } else {
        // Window wraps midnight, e.g. 22 → 6
        return h >= QUIET_HOURS_START || h < QUIET_HOURS_END;
    }
#else
    return false;
#endif
}

uint64_t getMicrosecondsTillNextWake(unsigned long delta, unsigned long deltaSinceTimeObtain) {
    const long intervalSeconds = (long)REFRESH_INTERVAL_HOURS * 3600L;

    if (!timeWorking) {
      // No wall-clock time available — fall back to a fixed-length sleep,
      // compensated for however long this wake has already been running.
      unsigned long totalRuntimeMs = millis() - delta;
      long totalRuntimeSeconds = (long)(totalRuntimeMs / 1000);
      long sleepSeconds = intervalSeconds - totalRuntimeSeconds;
      if (sleepSeconds < 1) sleepSeconds = 1;
      Serial.println("No time — fallback sleep: " + String(sleepSeconds) + " s");
      return (uint64_t)sleepSeconds * 1000000ULL;
    }

    Serial.println(&timeinfo, "Current time: %A, %B %d %Y %H:%M:%S");

    // Seconds since midnight, projected forward by however long this wake has run.
    long currentSeconds = (long)timeinfo.tm_hour * 3600L
                         + (long)timeinfo.tm_min * 60L
                         + (long)timeinfo.tm_sec;
    long elapsedSeconds = (long)((millis() - deltaSinceTimeObtain) / 1000);
    currentSeconds += elapsedSeconds;

    // Align to the next REFRESH_INTERVAL_HOURS boundary (e.g. the top of the next hour).
    long secondsSinceBoundary = currentSeconds % intervalSeconds;
    long sleepSeconds = intervalSeconds - secondsSinceBoundary;
    if (sleepSeconds <= 0) sleepSeconds = intervalSeconds;

    Serial.println("Sleeping " + String(sleepSeconds) + " s till next "
                   + String(REFRESH_INTERVAL_HOURS) + "-hour boundary");
    return (uint64_t)sleepSeconds * 1000000ULL;
}

uint64_t getMicrosecondsTillQuietHoursEnd(unsigned long delta, unsigned long deltaSinceTimeObtain) {
    if (!timeWorking) {
      // Shouldn't happen — isWithinQuietHours() requires known time — but fall
      // back safely rather than sleeping on a bogus calculation.
      return getMicrosecondsTillNextWake(delta, deltaSinceTimeObtain);
    }

    const long secondsInDay = 24L * 3600L;
    long currentSeconds = (long)timeinfo.tm_hour * 3600L
                         + (long)timeinfo.tm_min * 60L
                         + (long)timeinfo.tm_sec;
    long elapsedSeconds = (long)((millis() - deltaSinceTimeObtain) / 1000);
    currentSeconds = (currentSeconds + elapsedSeconds) % secondsInDay;

    long endSeconds = (long)QUIET_HOURS_END * 3600L;
    long sleepSeconds = endSeconds - currentSeconds;
    if (sleepSeconds <= 0) sleepSeconds += secondsInDay;  // window wraps past midnight

    Serial.println("Within quiet hours — sleeping " + String(sleepSeconds)
                   + " s until window end.");
    return (uint64_t)sleepSeconds * 1000000ULL;
}

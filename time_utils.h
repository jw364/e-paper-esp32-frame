#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
// #include "time.h"
#include <ArduinoJson.h>

#define USE_MOCK_TIME 0

#if USE_MOCK_TIME
bool getMockLocalTime(struct tm *timeinfo);
#endif

// ── Refresh / wake scheduling ─────────────────────────────────────────────────
#define REFRESH_INTERVAL_HOURS     1    // Wake and refresh the display every N hours

// Hourly wakes read the RTC-backed system clock only; WiFi/NTP is contacted just
// on first boot (or after power loss) and then every NTP_SYNC_EVERY_N_WAKES wakes
// to correct drift. If time has never been synced, WiFi is retried every wake.
#define NTP_SYNC_EVERY_N_WAKES    24    // ≈ once/day at REFRESH_INTERVAL_HOURS = 1

// ── Quiet hours (optional) ────────────────────────────────────────────────────
// Comment out QUIET_HOURS_ENABLED to disable. When enabled and the current time
// (if known) falls within [QUIET_HOURS_START, QUIET_HOURS_END), the refresh is
// skipped and the device sleeps directly until the window ends.
#define QUIET_HOURS_ENABLED
#define QUIET_HOURS_START          0    // inclusive hour, 0-23
#define QUIET_HOURS_END            7    // exclusive hour, 0-23

// External variable declarations
extern const char* ntpServer;
extern const long gmtOffset_sec;
extern const int daylightOffset_sec;
extern bool wifiWorking;
extern bool timeWorking;
extern struct tm timeinfo;

// Wake counter and one-time-sync flag, persisted in RTC memory across deep sleep
// (reset to 0/false only on power loss or cold boot).
extern uint32_t wakeCounter;
extern bool     timeSyncedOnce;

// Function declarations
void initializeWifi();
void initializeTime();

// Gates WiFi/NTP per NTP_SYNC_EVERY_N_WAKES and the "never synced yet" case;
// otherwise reads the RTC-backed system clock directly with no WiFi. Returns
// true if timeinfo holds a usable time after the call.
bool syncTimeIfNeeded();

// True if QUIET_HOURS_ENABLED is defined, current time is known, and the
// current hour falls within the configured quiet window.
bool isWithinQuietHours();

uint64_t getMicrosecondsTillNextWake(unsigned long delta, unsigned long deltaSinceTimeObtain);
uint64_t getMicrosecondsTillQuietHoursEnd(unsigned long delta, unsigned long deltaSinceTimeObtain);

#endif // TIME_UTILS_H
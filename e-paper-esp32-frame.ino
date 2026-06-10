/**
 * E-Paper Picture Frame — LILYGO T7-S3 V1.1 + Waveshare 3.6" E Ink Spectra 6
 *
 * Storage  : LittleFS on the 16 MB internal flash
 * Images   : Pre-dithered 4-bit E6 binary files, 2 pixels/byte, 120 000 bytes each
 *            /a0/00.bin … /a0/24.bin   (Album 0, 25 photos)
 *            /a1/00.bin … /a1/24.bin   (Album 1, 25 photos)
 *
 * Behaviour:
 *   - Wakes every hour (RTC timer) and advances to the next photo in the current album.
 *   - Button (GPIO 0, active-LOW) wakes the device immediately.
 *       Short press  (< 2 s held)  → advance to next photo right away.
 *       Long gesture (≥ 2 s hold, release, then 2 presses within 10 s)
 *                                  → switch album (0 ↔ 1) and reset to photo 0.
 *
 * Flashing images:
 *   1. Run bmpConverter/converter.py to produce the data/ directory.
 *   2. Upload with the Arduino IDE LittleFS Data Uploader plug-in
 *      (or `pio run -t uploadfs` in PlatformIO).
 *
 * Partition table: use partitions.csv (see that file) — 4 MB app + ~12 MB LittleFS.
 */

#include <SPI.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "epd3in6e.h"

// ── Pin definitions ──────────────────────────────────────────────────────────
#define BUTTON_PIN   0   // Boot / user button, active-LOW, has internal pull-up
#define BAT_ADC_PIN  2   // Battery voltage via 1:1 resistor divider on T7-S3 V1.1

// ── Constants ────────────────────────────────────────────────────────────────
#define ALBUM_SIZE          25   // photos per album
#define SLEEP_SECONDS    3600ULL // 1 hour between automatic photo changes
#define GESTURE_HOLD_MS  2000    // hold duration required to enter switch mode
#define GESTURE_TIMEOUT  10000   // ms to complete the 2-press confirmation

Preferences preferences;
Epd epd;

// ── Battery ──────────────────────────────────────────────────────────────────
float readBatteryVoltage() {
    // Average several samples; T7-S3 has a 1:1 (200k + 200k) voltage divider.
    const int SAMPLES = 10;
    uint32_t total = 0;
    for (int i = 0; i < SAMPLES; i++) {
        total += analogReadMilliVolts(BAT_ADC_PIN);
        delay(3);
    }
    return (total / SAMPLES) * 2.0f / 1000.0f;  // ×2 for divider, /1000 → volts
}

// ── Sleep ────────────────────────────────────────────────────────────────────
void hibernate() {
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0); // wake on button LOW
    Serial.println("Entering deep sleep...");
    Serial.flush();
    esp_deep_sleep_start();
}

void hibernateLowBattery() {
    // Battery critically low — sleep indefinitely to protect the cell.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    Serial.println("Battery critical — indefinite sleep.");
    Serial.flush();
    esp_deep_sleep_start();
}

// ── Button gesture ───────────────────────────────────────────────────────────
// Returns true when the full album-switch gesture is detected:
//   hold ≥ 2 s → release → 2 presses within GESTURE_TIMEOUT ms.
bool detectAlbumSwitchGesture() {
    unsigned long holdStart = millis();

    // Wait to see if the button is held for GESTURE_HOLD_MS
    while (digitalRead(BUTTON_PIN) == LOW) {
        if (millis() - holdStart >= GESTURE_HOLD_MS) {
            // Held long enough — wait for release
            while (digitalRead(BUTTON_PIN) == LOW) delay(10);
            delay(50);  // debounce after release

            // Collect 2 presses within the timeout window
            int pressCount = 0;
            unsigned long deadline = millis() + GESTURE_TIMEOUT;
            bool lastPinState = HIGH;

            while (millis() < deadline && pressCount < 2) {
                int cur = digitalRead(BUTTON_PIN);
                if (lastPinState == HIGH && cur == LOW) {
                    pressCount++;
                    delay(50);  // debounce
                }
                lastPinState = cur;
                delay(10);
            }
            return pressCount >= 2;
        }
        delay(10);
    }
    return false;  // released before 2 s → short press, not a gesture
}

// ── Display ──────────────────────────────────────────────────────────────────
void displayPhoto(uint8_t album, uint8_t photo) {
    char path[32];
    snprintf(path, sizeof(path), "/a%d/%02d.bin", album, photo);
    Serial.printf("Display: %s\n", path);

    File file = LittleFS.open(path, "r");
    if (!file) {
        Serial.printf("File not found: %s\n", path);
        // Graceful fallback: show a white screen so the user knows the frame is alive
        epd.Clear(EPD_WHITE);
        return;
    }

    epd.SendCommand(0x10);  // Begin data transmission

    uint8_t buf[512];
    size_t totalBytes = 0;
    while (file.available()) {
        size_t n = file.read(buf, sizeof(buf));
        for (size_t i = 0; i < n; i++) {
            epd.SendData(buf[i]);
        }
        totalBytes += n;
    }
    file.close();
    Serial.printf("Sent %u bytes\n", totalBytes);

    epd.TurnOnDisplay();
    epd.Sleep();
}

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    setCpuFrequencyMhz(80);
    Serial.begin(115200);

    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

    preferences.begin("frame", false);
    uint8_t album = preferences.getUChar("album", 0);
    uint8_t photo = preferences.getUChar("photo", 0);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // ── Determine next album / photo based on wake reason ──────────────────
    if (wakeReason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("Wake: button");
        if (detectAlbumSwitchGesture()) {
            album = 1 - album;   // toggle 0 ↔ 1
            photo = 0;
            Serial.printf("Album switched to %d\n", album);
        } else {
            // Short press: advance immediately to the next photo
            photo = (photo + 1) % ALBUM_SIZE;
            Serial.printf("Next photo: %d\n", photo);
        }
    } else if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Wake: timer");
        photo = (photo + 1) % ALBUM_SIZE;
    } else {
        Serial.println("Wake: first boot / reset");
        // Retain stored album and photo (or defaults 0,0 on very first boot)
    }

    preferences.putUChar("album", album);
    preferences.putUChar("photo", photo);
    preferences.end();

    // ── Battery check ───────────────────────────────────────────────────────
    float bat = readBatteryVoltage();
    Serial.printf("Battery: %.2f V\n", bat);
    if (bat < 3.0f && bat > 0.5f) {  // > 0.5 avoids false positives on USB power
        hibernateLowBattery();
        return;
    }

    // ── Filesystem ──────────────────────────────────────────────────────────
    if (!LittleFS.begin(false)) {
        Serial.println("LittleFS mount failed — has the filesystem been uploaded?");
        hibernate();
        return;
    }

    // ── Display ─────────────────────────────────────────────────────────────
    if (epd.Init() != 0) {
        Serial.println("EPD init failed");
        hibernate();
        return;
    }

    displayPhoto(album, photo);

    hibernate();
}

void loop() {
    // Should never be reached — hibernate() calls esp_deep_sleep_start()
    hibernate();
}

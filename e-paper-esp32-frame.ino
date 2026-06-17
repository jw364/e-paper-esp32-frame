/**
 * e-paper-esp32-frame.ino
 *
 * Target hardware:
 *   MCU      : LILYGO T7 S3 V1.1 (ESP32-S3, 8 MB OPI PSRAM, 16 MB Flash)
 *   Display  : Waveshare 7.3" Spectra 6 (E6) Full-Color E-Paper (800×480)
 *   Driver   : Waveshare HAT+ Driver Board
 *   Storage  : MicroSD card module
 *   Power SW : AO3401 P-channel MOSFET (high-side, 3.3 V rail)
 *   Battery  : 3.7 V 1200 mAh LiPo
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * PIN MAP
 * ─────────────────────────────────────────────────────────────────────────────
 *  GPIO  Purpose
 *  ────  ──────────────────────────────────────────────────────────────────────
 *   10   EPD CS          (SPI2 FSPI)
 *   11   EPD MOSI/DIN    (SPI2 FSPI)
 *   12   EPD SCLK        (SPI2 FSPI)
 *   13   EPD DC
 *   14   EPD RST
 *   15   EPD BUSY
 *    4   SD CS           (SPI3 HSPI)
 *    5   SD MOSI         (SPI3 HSPI)
 *    6   SD SCLK         (SPI3 HSPI)
 *    7   SD MISO         (SPI3 HSPI)
 *   16   MOSFET gate     (LOW = peripherals ON; HIGH = peripherals OFF)
 *    2   Battery ADC     (onboard /2 voltage divider → analogReadMilliVolts × 2)
 *    1   Album button    (active LOW; internal pull-up enabled)
 *   17   Onboard LED     (do not use for other functions)
 *    0   BOOT button     (do not use; strapping pin)
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * SD CARD DIRECTORY STRUCTURE
 *   /albumA/         ← album A images (up to 25 × 800×480 BMP)
 *   /albumA/info.txt ← any content; change triggers rescan
 *   /albumB/         ← album B images
 *   /albumB/info.txt
 *   /setup.json      ← WiFi credentials
 *   /fileStringA.txt ← auto-generated; do not edit
 *   /fileStringB.txt ← auto-generated; do not edit
 *
 * ALBUM SWITCHING (button on GPIO1)
 *   1. Hold button ≥ ALBUM_HOLD_DURATION_MS  (~2.5 s)
 *   2. Release
 *   3. Press twice within ALBUM_CONFIRM_WINDOW_MS (3 s each)
 *   Short presses and partial sequences are silently ignored.
 */

#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include "epd7in3combined.h"
#include <Preferences.h>
#include <algorithm>
#include <vector>
#include "time_utils.h"
#include <climits>

// ── Pin definitions ───────────────────────────────────────────────────────────

// AO3401 P-channel MOSFET power switch
// LOW  = gate pulled toward GND → Vgs negative → MOSFET ON  → peripherals powered
// HIGH = gate at Vsource (3.3 V) → Vgs = 0 → MOSFET OFF → peripherals unpowered
// External: 10 kΩ series resistor gate→GPIO16; 100 kΩ pull-up gate→3.3 V
#define MOSFET_PIN      16

// SD card SPI (HSPI / SPI3, bus index 1 on ESP32-S3)
#define SD_SCLK_PIN      6
#define SD_MISO_PIN      7
#define SD_MOSI_PIN      5
#define SD_CS_PIN        4

// Battery ADC — T7 S3 onboard /2 divider; ADC1 channel, safe with WiFi
#define BAT_ADC_PIN      2

// Album switch button — active LOW, internal pull-up
#define BUTTON_PIN       1

// ── Album-switch button timing constants ─────────────────────────────────────
// Adjust these values to change button feel.
#define BUTTON_DEBOUNCE_MS        50    // Stable-state time required for a valid edge (ms)
#define ALBUM_HOLD_DURATION_MS  2500    // Hold duration that initiates album-switch flow (ms)
#define ALBUM_CONFIRM_WINDOW_MS 3000    // Time window for each confirmation press (ms)
#define BUTTON_POLL_MS            10    // Button state polling interval (ms)

// ── Global objects ────────────────────────────────────────────────────────────
Preferences preferences;
Epd         epd;
SPIClass    sd_spi(HSPI);   // HSPI = SPI3, bus index 1 on ESP32-S3

unsigned long delta;                  // millis() at wake; used for sleep calculation
unsigned long deltaSinceTimeObtain;   // millis() after NTP sync

// Convenience wrappers used by drawBmp()
uint16_t width()  { return EPD_WIDTH;  }
uint16_t height() { return EPD_HEIGHT; }

// ── E6 Spectra 6 color palette (6 colors, used for Floyd–Steinberg dithering) -
// NOTE: BMP stores pixels in B,G,R order; palette entries below are ordered
// to match — B and R appear swapped vs. their display names, but the
// depalette() nearest-neighbor search and the display color constants are
// mutually consistent so the rendered output is correct.
#if defined(DISPLAY_TYPE_E)
  uint8_t colorPallete[6 * 3] = {
    0,   0,   0,    // Black
    255, 255, 255,  // White
    255, 255, 0,    // Yellow  (in BMP order: B=255,G=255,R=0)
    255, 0,   0,    // Red     (in BMP order: B=255,G=0,  R=0)
    0,   0,   255,  // Blue    (in BMP order: B=0,  G=0,  R=255)
    0,   255, 0,    // Green
  };
#elif defined(DISPLAY_TYPE_F)
  uint8_t colorPallete[7 * 3] = {
    0,   0,   0,
    255, 255, 255,
    67,  138, 28,
    100, 64,  255,
    191, 0,   0,
    255, 243, 56,
    232, 126, 0,
  };
#endif


// ═════════════════════════════════════════════════════════════════════════════
// BMP helper readers
// ═════════════════════════════════════════════════════════════════════════════

uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read();
  ((uint8_t *)&result)[1] = f.read();
  return result;
}

uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read();
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read();
  return result;
}


// ═════════════════════════════════════════════════════════════════════════════
// Battery voltage (ESP32-S3 — no deprecated esp_adc_cal required)
// analogReadMilliVolts() uses eFuse-calibrated ADC on S3 automatically.
// ═════════════════════════════════════════════════════════════════════════════

float readBattery() {
  const int samples = 16;
  uint32_t total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }
  // Onboard /2 divider: actual battery voltage = ADC reading × 2
  return (float)total / samples * 2.0f / 1000.0f;
}


// ═════════════════════════════════════════════════════════════════════════════
// Album helper functions
// ═════════════════════════════════════════════════════════════════════════════

uint8_t getAlbumIndex() {
  return preferences.getUChar("albumIndex", 0);  // 0 = Album A, 1 = Album B
}

const char* getAlbumDir(uint8_t idx) {
  return (idx == 0) ? "/albumA" : "/albumB";
}

// Per-album NVS key names
const char* getCheckerKey(uint8_t idx)    { return (idx == 0) ? "checkerA"    : "checkerB";    }
const char* getFileCountKey(uint8_t idx)  { return (idx == 0) ? "fileCountA"  : "fileCountB";  }
const char* getImageIndexKey(uint8_t idx) { return (idx == 0) ? "imageIndexA" : "imageIndexB"; }

// Per-album file-list cache paths on SD
const char* getFileStringPath(uint8_t idx) {
  return (idx == 0) ? "/fileStringA.txt" : "/fileStringB.txt";
}


// ═════════════════════════════════════════════════════════════════════════════
// Album-switching button state machine
// Called once at the top of setup() before any display or SD work.
// Blocks for at most ALBUM_HOLD_DURATION_MS + 2×ALBUM_CONFIRM_WINDOW_MS ≈ 8.5 s.
// ═════════════════════════════════════════════════════════════════════════════

static inline bool readButton() {
  return digitalRead(BUTTON_PIN) == LOW;  // Active LOW
}

// Polls until button reaches wantPressed state (stable for BUTTON_DEBOUNCE_MS)
// or timeout expires.  Returns true if the desired state was reached.
static bool waitForButtonState(bool wantPressed, unsigned long timeoutMs) {
  unsigned long deadline     = millis() + timeoutMs;
  int           stableCount  = 0;
  const int     stableNeeded = BUTTON_DEBOUNCE_MS / BUTTON_POLL_MS;

  while (millis() < deadline) {
    if (readButton() == wantPressed) {
      if (++stableCount >= stableNeeded) return true;
    } else {
      stableCount = 0;
    }
    delay(BUTTON_POLL_MS);
  }
  return false;
}

void checkAndHandleAlbumButton() {
  // Fast path: button not active — return immediately, no delay
  if (!readButton()) return;

  Serial.println("[BTN] Button active at boot.");
  Serial.println("[BTN] Hold for " + String(ALBUM_HOLD_DURATION_MS / 1000.0f, 1)
                 + " s to initiate album switch...");

  // ── Step 1: wait for hold duration ──────────────────────────────────────
  unsigned long holdStart = millis();
  bool held = false;
  while (readButton()) {
    if (millis() - holdStart >= ALBUM_HOLD_DURATION_MS) {
      held = true;
      break;
    }
    delay(BUTTON_POLL_MS);
  }
  if (!held) {
    Serial.println("[BTN] Short press ignored (below hold threshold).");
    return;
  }

  Serial.println("[BTN] Hold detected! Release button to continue.");

  // ── Step 2: wait for release ─────────────────────────────────────────────
  if (!waitForButtonState(false, 5000)) {
    Serial.println("[BTN] Album switch CANCELLED — button appears stuck.");
    return;
  }
  delay(BUTTON_DEBOUNCE_MS);

  // ── Step 3: first confirmation press ────────────────────────────────────
  Serial.println("[BTN] Awaiting confirmation press 1/2 (within "
                 + String(ALBUM_CONFIRM_WINDOW_MS / 1000) + " s)...");
  if (!waitForButtonState(true, ALBUM_CONFIRM_WINDOW_MS)) {
    Serial.println("[BTN] Album switch CANCELLED — no confirmation press 1/2.");
    return;
  }
  Serial.println("[BTN] Confirmation press 1/2 received.");
  waitForButtonState(false, 1000);  // wait for release
  delay(BUTTON_DEBOUNCE_MS);

  // ── Step 4: second confirmation press ───────────────────────────────────
  Serial.println("[BTN] Awaiting confirmation press 2/2 (within "
                 + String(ALBUM_CONFIRM_WINDOW_MS / 1000) + " s)...");
  if (!waitForButtonState(true, ALBUM_CONFIRM_WINDOW_MS)) {
    Serial.println("[BTN] Album switch CANCELLED — no confirmation press 2/2.");
    return;
  }
  Serial.println("[BTN] Confirmation press 2/2 received.");
  waitForButtonState(false, 1000);

  // ── Step 5: commit the switch ────────────────────────────────────────────
  uint8_t currentAlbum = getAlbumIndex();
  uint8_t newAlbum     = currentAlbum ? 0 : 1;
  preferences.putUChar("albumIndex", newAlbum);

  Serial.println("[BTN] *** ALBUM SWITCHED: "
                 + String(currentAlbum == 0 ? "A" : "B")
                 + " → "
                 + String(newAlbum == 0 ? "A" : "B") + " ***");
}


// ═════════════════════════════════════════════════════════════════════════════
// Deep sleep helper
// ═════════════════════════════════════════════════════════════════════════════

void hibernate() {
  Serial.println("Entering deep sleep...");
  // Hold MOSFET_PIN HIGH (peripherals off) during deep sleep.
  // gpio_hold_en() latches the current output level; gpio_deep_sleep_hold_en()
  // activates this for all held pins across the sleep cycle.
  gpio_hold_en((gpio_num_t)MOSFET_PIN);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep(
    static_cast<uint64_t>(getSecondsTillNextImage(delta, deltaSinceTimeObtain)) * 1000000ULL
  );
}


// ═════════════════════════════════════════════════════════════════════════════
// SD file management — album-aware
// ═════════════════════════════════════════════════════════════════════════════

void checkSDFiles() {
  uint8_t     albumIdx = getAlbumIndex();
  const char* albumDir = getAlbumDir(albumIdx);

  Serial.println("Scanning album: " + String(albumDir));

  // Check for info.txt inside the album directory
  String infoPath = String(albumDir) + "/info.txt";
  File infoFile = SD.open(infoPath);
  if (!infoFile) {
    Serial.println("No info.txt found in " + String(albumDir) + " — skipping rescan.");
    return;
  }
  String infoText = "";
  while (infoFile.available()) infoText += (char)infoFile.read();
  infoFile.close();
  Serial.println("info.txt: " + infoText);

  // Only rescan if info.txt changed (prevents unnecessary SD access on every boot)
  const char* checkerKey = getCheckerKey(albumIdx);
  if (preferences.getString(checkerKey, "") == infoText) {
    Serial.println("Album unchanged — using cached file list.");
    return;
  }

  Serial.println("Album changed or first run — rescanning " + String(albumDir));

  File root = SD.open(albumDir);
  if (!root || !root.isDirectory()) {
    Serial.println("Album directory not found: " + String(albumDir));
    return;
  }

  std::vector<String> bmpFiles;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    String name = String(entry.name());
    // Match case-insensitively by checking last 4 chars
    String ext = name.length() >= 4 ? name.substring(name.length() - 4) : "";
    ext.toLowerCase();
    if (ext == ".bmp") {
      bmpFiles.push_back(name);
      Serial.println("  + " + name);
    }
    entry.close();
  }
  root.close();

  std::sort(bmpFiles.begin(), bmpFiles.end());

  String fileString = "";
  for (auto& fn : bmpFiles) fileString += fn + ",";

  preferences.putUInt(getFileCountKey(albumIdx),  (uint32_t)bmpFiles.size());
  preferences.putUInt(getImageIndexKey(albumIdx),  0);
  preferences.putString(checkerKey,               infoText);

  const char* fsPath = getFileStringPath(albumIdx);
  File fsFile = SD.open(fsPath, FILE_WRITE);
  if (!fsFile) {
    Serial.println("Failed to write file list to " + String(fsPath));
    return;
  }
  fsFile.print(fileString);
  fsFile.close();

  Serial.println("Cached " + String(bmpFiles.size())
                 + " files to " + String(fsPath));
}

String getNextFile() {
  uint8_t     albumIdx = getAlbumIndex();
  const char* albumDir = getAlbumDir(albumIdx);
  const char* fsPath   = getFileStringPath(albumIdx);

  File file = SD.open(fsPath);
  if (!file) {
    Serial.println("File list not found: " + String(fsPath));
    return "";
  }
  String fileString = "";
  while (file.available()) fileString += (char)file.read();
  file.close();

  // Determine display date
  String date;
  if (timeWorking) {
    Serial.println("Hour: " + String(timeinfo.tm_hour));
    if (timeinfo.tm_hour < 9) {
      // Before 09:00 → show yesterday's image
      time_t yesterday = mktime(&timeinfo) - 86400;
      struct tm* pdi   = localtime(&yesterday);
      char buf[6];
      snprintf(buf, sizeof(buf), "%02d.%02d", pdi->tm_mday, pdi->tm_mon + 1);
      date = String(buf);
    } else {
      char buf[6];
      snprintf(buf, sizeof(buf), "%02d.%02d", timeinfo.tm_mday, timeinfo.tm_mon + 1);
      date = String(buf);
    }
  } else {
    // No time available — advance stored date by one day
    date = preferences.getString("date", "01.01");
    int day   = date.substring(0, 2).toInt();
    int month = date.substring(3, 5).toInt();
    struct tm t = {0};
    t.tm_year  = 100;       // year 2000 (leap year, handles Feb 29)
    t.tm_mon   = month - 1;
    t.tm_mday  = day + 1;
    mktime(&t);             // normalises overflow (e.g. Jan 32 → Feb 1)
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d.%02d", t.tm_mday, t.tm_mon + 1);
    date = String(buf);
    Serial.println("Fallback date: " + date);
  }

  Serial.println("Looking for date: " + date);
  preferences.putString("date", date);

  // Search for a date-labelled file first
  int    start    = 0;
  int    end      = fileString.indexOf(',', start);
  String nextFile = "";

  while (end != -1) {
    String candidate = fileString.substring(start, end);
    if (candidate.indexOf(date) != -1) {
      nextFile = candidate;
      break;
    }
    start = end + 1;
    end   = fileString.indexOf(',', start);
  }

  if (nextFile != "") {
    return String(albumDir) + "/" + nextFile;
  }

  // No date match — sequential rotation
  uint32_t fileCount  = preferences.getUInt(getFileCountKey(albumIdx),  0);
  uint32_t imageIndex = preferences.getUInt(getImageIndexKey(albumIdx), 0);
  uint32_t usedIndex  = imageIndex;
  preferences.putUInt(getImageIndexKey(albumIdx),
                      (imageIndex >= fileCount - 1) ? 0 : imageIndex + 1);

  start = 0;
  end   = fileString.indexOf(',', start);
  for (uint32_t i = 0; i < usedIndex && end != -1; i++) {
    start = end + 1;
    end   = fileString.indexOf(',', start);
  }
  nextFile = (end != -1) ? fileString.substring(start, end) : "";
  Serial.println("Sequential [" + String(usedIndex) + "]: " + nextFile);
  return String(albumDir) + "/" + nextFile;
}


// ═════════════════════════════════════════════════════════════════════════════
// Nearest-color lookup for Floyd–Steinberg dithering
// ═════════════════════════════════════════════════════════════════════════════

int depalette(uint8_t r, uint8_t g, uint8_t b) {
  int bestc   = 0;
  int mindiff = INT_MAX;
  int nColors = (int)(sizeof(colorPallete) / 3);
  for (int p = 0; p < nColors; p++) {
    int dr = (int)r - (int)colorPallete[p * 3 + 0];
    int dg = (int)g - (int)colorPallete[p * 3 + 1];
    int db = (int)b - (int)colorPallete[p * 3 + 2];
    int d  = dr * dr + dg * dg + db * db;
    if (d < mindiff) { mindiff = d; bestc = p; }
  }
  return bestc;
}


// ═════════════════════════════════════════════════════════════════════════════
// BMP rendering — Floyd–Steinberg dithering to 6-color E6 palette
// Streams pixel data directly to the display over SPI (no full-frame buffer).
// ═════════════════════════════════════════════════════════════════════════════

bool drawBmp(const char *filename) {
  Serial.println("Drawing: " + String(filename));

  fs::File bmpFS = SD.open(filename);
  if (!bmpFS) {
    Serial.println("File open failed: " + String(filename));
    return false;
  }

  uint32_t seekOffset, headerSize;
  uint32_t paletteSize = 0;
  uint8_t  r, g, b;
  uint16_t bitDepth;

  uint16_t magic = read16(bmpFS);
  if (magic != ('B' | ('M' << 8))) {
    Serial.println("Not a valid BMP.");
    bmpFS.close();
    return false;
  }

  read32(bmpFS);                    // file size (unused)
  read32(bmpFS);                    // reserved
  seekOffset = read32(bmpFS);       // offset to pixel data
  headerSize = read32(bmpFS);       // DIB header size
  uint32_t w = read32(bmpFS);       // image width
  uint32_t h = read32(bmpFS);       // image height
  read16(bmpFS);                    // colour planes (must be 1)
  bitDepth = read16(bmpFS);

  if (read32(bmpFS) != 0 ||
      (bitDepth != 24 && bitDepth != 8 && bitDepth != 4 && bitDepth != 1)) {
    Serial.println("Unsupported BMP format (compression or bit depth).");
    bmpFS.close();
    return false;
  }

  uint32_t palette[256];
  if (bitDepth <= 8) {
    read32(bmpFS); read32(bmpFS); read32(bmpFS);  // skip image size, resolution fields
    paletteSize = read32(bmpFS);
    if (paletteSize == 0) paletteSize = 1u << bitDepth;
    bmpFS.seek(14 + headerSize);
    for (uint32_t i = 0; i < paletteSize; i++) palette[i] = read32(bmpFS);
  }

  // Centre image if smaller than display
  uint16_t x = (uint16_t)((width()  - w) / 2);
  uint16_t y = (uint16_t)((height() - h) / 2);

  bmpFS.seek(seekOffset);

  uint32_t lineSize = ((bitDepth * w + 31) >> 5) * 4;  // padded to 4-byte boundary
  uint8_t  lineBuffer[lineSize];
  uint8_t  nextLineBuffer[lineSize];

  // Begin streaming pixel data to display
  epd.SendCommand(0x10);
  epd.EPD_7IN3F_Draw_Blank(y, width(), EPD_WHITE);   // top border

  // BMP rows are stored bottom-up; we read the bottom row first and walk upwards
  bmpFS.read(lineBuffer, sizeof(lineBuffer));
  std::reverse(lineBuffer, lineBuffer + sizeof(lineBuffer));

  float batteryVolts = readBattery();
  Serial.println("Battery: " + String(batteryVolts, 2) + " V");

  for (int16_t row = (int16_t)h - 1; row >= 0; row--) {
    epd.EPD_7IN3F_Draw_Blank(1, x, EPD_WHITE);  // left border

    if (row != 0) {
      bmpFS.read(nextLineBuffer, sizeof(nextLineBuffer));
      std::reverse(nextLineBuffer, nextLineBuffer + sizeof(nextLineBuffer));
    }

    uint8_t *bptr  = lineBuffer;
    uint8_t *bnptr = nextLineBuffer;
    uint8_t  output = 0;

    for (uint16_t col = 0; col < w; col++) {
      // Read next pixel (RGB values; BMP stores BGR so r/b are swapped here,
      // but the palette is defined consistently, so dithering is correct)
      if (bitDepth == 24) {
        r = *bptr++;
        g = *bptr++;
        b = *bptr++;
        bnptr += 3;
      } else {
        uint32_t c = 0;
        if (bitDepth == 8) {
          c = palette[*bptr++];
        } else if (bitDepth == 4) {
          c = palette[(*bptr >> ((col & 0x01) ? 0 : 4)) & 0x0F];
          if (col & 0x01) bptr++;
        } else {  // 1-bit
          c = palette[(*bptr >> (7 - (col & 0x07))) & 0x01];
          if ((col & 0x07) == 0x07) bptr++;
        }
        b = (uint8_t)c; g = (uint8_t)(c >> 8); r = (uint8_t)(c >> 16);
      }

      // Floyd–Steinberg error diffusion
      int indexColor = depalette(r, g, b);
      int errorR = (int)r - (int)colorPallete[indexColor * 3 + 0];
      int errorG = (int)g - (int)colorPallete[indexColor * 3 + 1];
      int errorB = (int)b - (int)colorPallete[indexColor * 3 + 2];

      // Distribute error to right neighbour (7/16)
      if (col < w - 1) {
        bptr[0] = (uint8_t)constrain(bptr[0] + (7 * errorR / 16), 0, 255);
        bptr[1] = (uint8_t)constrain(bptr[1] + (7 * errorG / 16), 0, 255);
        bptr[2] = (uint8_t)constrain(bptr[2] + (7 * errorB / 16), 0, 255);
      }
      // Distribute error to next row
      if (row > 0) {
        if (col > 0) {  // bottom-left (3/16)
          bnptr[-4] = (uint8_t)constrain(bnptr[-4] + (3 * errorR / 16), 0, 255);
          bnptr[-5] = (uint8_t)constrain(bnptr[-5] + (3 * errorG / 16), 0, 255);
          bnptr[-6] = (uint8_t)constrain(bnptr[-6] + (3 * errorB / 16), 0, 255);
        }
        bnptr[-1] = (uint8_t)constrain(bnptr[-1] + (5 * errorR / 16), 0, 255);  // bottom (5/16)
        bnptr[-2] = (uint8_t)constrain(bnptr[-2] + (5 * errorG / 16), 0, 255);
        bnptr[-3] = (uint8_t)constrain(bnptr[-3] + (5 * errorB / 16), 0, 255);
        if (col < w - 1) {  // bottom-right (1/16)
          bnptr[0] = (uint8_t)constrain(bnptr[0] + (1 * errorR / 16), 0, 255);
          bnptr[1] = (uint8_t)constrain(bnptr[1] + (1 * errorG / 16), 0, 255);
          bnptr[2] = (uint8_t)constrain(bnptr[2] + (1 * errorB / 16), 0, 255);
        }
      }

      // Map palette index to display colour constant
      uint8_t color;
      switch (indexColor) {
#if defined(DISPLAY_TYPE_E)
        case 0:  color = EPD_7IN3E_BLACK;  break;
        case 1:  color = EPD_7IN3E_WHITE;  break;
        case 2:  color = EPD_7IN3E_YELLOW; break;
        case 3:  color = EPD_7IN3E_RED;    break;
        case 4:  color = EPD_7IN3E_BLUE;   break;
        case 5:  color = EPD_7IN3E_GREEN;  break;
        default: color = EPD_7IN3E_WHITE;  break;
#elif defined(DISPLAY_TYPE_F)
        case 0:  color = EPD_7IN3F_BLACK;  break;
        case 1:  color = EPD_7IN3F_WHITE;  break;
        case 2:  color = EPD_7IN3F_GREEN;  break;
        case 3:  color = EPD_7IN3F_BLUE;   break;
        case 4:  color = EPD_7IN3F_RED;    break;
        case 5:  color = EPD_7IN3F_YELLOW; break;
        case 6:  color = EPD_7IN3F_ORANGE; break;
        default: color = EPD_7IN3F_WHITE;  break;
#endif
      }

      // Low-battery indicator: red square in top-left corner of image
      if (batteryVolts <= 3.3f && col <= 50 && row >= (int16_t)(h - 50)) {
        color = EPD_RED;
        if (batteryVolts < 3.1f) {
          Serial.println("Battery critically low — indefinite sleep.");
          // Disable all wakeup sources so device won't restart until recharged
          esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
          gpio_hold_en((gpio_num_t)MOSFET_PIN);
          gpio_deep_sleep_hold_en();
          esp_deep_sleep_start();
        }
      }

      // Pack two 4-bit colour values into one byte (high nibble first)
      if (col & 0x01) {
        output |= color;
        epd.SendData(output);
      } else {
        output = (uint8_t)(color << 4);
      }
    }  // col

    epd.EPD_7IN3F_Draw_Blank(1, x, EPD_WHITE);  // right border
    memcpy(lineBuffer, nextLineBuffer, sizeof(lineBuffer));
  }  // row

  epd.EPD_7IN3F_Draw_Blank(y, width(), EPD_WHITE);  // bottom border

  bmpFS.close();
  epd.TurnOnDisplay();
  epd.Sleep();
  return true;
}


// ═════════════════════════════════════════════════════════════════════════════
// setup() / loop()
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
  setCpuFrequencyMhz(80);  // Reduce CPU from 240 → 80 MHz; saves ~20 mA while awake
  Serial.begin(115200);
  delta = millis();

  preferences.begin("e-paper", false);

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Woke from deep sleep (timer).");
  } else {
    Serial.println("Cold boot.");
  }

  // Release hold on MOSFET_PIN set before the previous deep sleep
  gpio_hold_dis((gpio_num_t)MOSFET_PIN);

  // Drive MOSFET LOW → peripherals (SD card + HAT+ + display) powered ON
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);
  delay(20);  // Allow 3.3 V rail to stabilise across all peripherals

  // Configure album button before any blocking waits
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // ── Check for album-switch request ────────────────────────────────────────
  // Preferences must be open before this because checkAndHandleAlbumButton()
  // reads and writes albumIndex.
  checkAndHandleAlbumButton();

  // ── SD card (HSPI / SPI3) ─────────────────────────────────────────────────
  sd_spi.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, sd_spi)) {
    Serial.println("SD mount failed — hibernating.");
    hibernate();
  }

  // ── WiFi + NTP ────────────────────────────────────────────────────────────
  initializeWifi();
  initializeTime();
  deltaSinceTimeObtain = millis();

  // ── E-paper display ───────────────────────────────────────────────────────
  if (epd.Init() != 0) {
    Serial.println("EPD init failed — hibernating.");
    hibernate();
  }
  Serial.println("EPD init OK.");

  // ── Scan SD, pick next file, render ───────────────────────────────────────
  checkSDFiles();
  String file = getNextFile();
  if (file.length() == 0) {
    Serial.println("No image file found — hibernating.");
    // Put display to sleep before giving up
    epd.Sleep();
  } else {
    drawBmp(file.c_str());
  }

  // Power off peripherals (MOSFET HIGH)
  digitalWrite(MOSFET_PIN, HIGH);
  preferences.end();
}

void loop() {
  hibernate();
}

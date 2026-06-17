# E-Paper ESP32-S3 Frame

Daily-updating e-paper picture frame with two independently switchable photo albums, Floyd–Steinberg dithering for rich color reproduction, and ultra-low deep-sleep power consumption.

![ESP e-paper frame](images/e-paper-esp32-frame.jpg?raw=true)
![ESP e-paper frame backside](images/e-paper-esp32-frame-backside.jpg?raw=true)

---

## Hardware

| Component | Notes |
|---|---|
| [LILYGO T7 S3 V1.1](https://www.lilygo.cc/products/t7-s3) | ESP32-S3, 8 MB OPI PSRAM, 16 MB Flash |
| [Waveshare 7.3" Spectra 6 E6 E-Paper + HAT+](https://www.waveshare.com/product/displays/e-paper/epaper-1/7.3inch-e-paper-hat-e.htm) | 800×480, 6-color, SPI |
| MicroSD card module | SPI, 3.3 V |
| AO3401 P-channel MOSFET | High-side power switch for SD + display |
| 3.7 V 1200 mAh LiPo | Connected to T7 S3 battery connector |
| 10 kΩ resistor (×1) | MOSFET gate series resistor |
| 100 kΩ resistor (×1) | MOSFET gate pull-up to 3.3 V |
| Momentary push button | Album switch, wired to GPIO1 and GND |

---

## Pin Map

| GPIO | Function | Direction |
|---|---|---|
| 10 | EPD CS (SPI2 FSPI) | Output |
| 11 | EPD MOSI / DIN (SPI2 FSPI) | Output |
| 12 | EPD SCLK (SPI2 FSPI) | Output |
| 13 | EPD DC | Output |
| 14 | EPD RST | Output |
| 15 | EPD BUSY | Input |
| 4 | SD CS (SPI3 HSPI) | Output |
| 5 | SD MOSI (SPI3 HSPI) | Output |
| 6 | SD SCLK (SPI3 HSPI) | Output |
| 7 | SD MISO (SPI3 HSPI) | Input |
| 16 | AO3401 gate (LOW=ON, HIGH=OFF) | Output |
| 2 | Battery ADC (onboard /2 divider) | Analog Input |
| 1 | Album switch button (active LOW) | Input |
| 17 | Onboard LED — **do not use** | — |
| 0 | BOOT button / strapping — **do not use** | — |
| 19/20 | USB D−/D+ — **do not use** | — |

---

## Wiring

### AO3401 MOSFET Power Switch

The AO3401 is a P-channel MOSFET acting as a high-side switch on the 3.3 V rail.
When GPIO16 is driven LOW, Vgs ≈ −3.3 V → MOSFET conducts → peripherals powered.
When GPIO16 is HIGH (or floating), Vgs ≈ 0 V → MOSFET off → peripherals unpowered.

```
T7 S3 3.3V ───────────────────────┬──── AO3401 Source
                                   │
                                 [100kΩ pull-up]   ← ensures OFF during boot/sleep
                                   │
GPIO16 ────── [10kΩ series] ──────┤──── AO3401 Gate
                                   
AO3401 Drain ──────────────────────┬──── HAT+ VCC (3.3 V pin on HAT+ connector)
                                   └──── SD module VCC
T7 S3 GND ─────────────────────────┬──── HAT+ GND
                                   └──── SD module GND
```

**Why the 100 kΩ pull-up?**
During boot, GPIO16 is briefly in high-impedance state. Without the pull-up the gate
would float, causing momentary/unpredictable power to the display. The 100 kΩ pull-up
holds the gate at source potential (Vgs = 0) → MOSFET stays off until firmware
deliberately drives GPIO16 LOW.

**Why the 10 kΩ series resistor?**
Limits in-rush gate charge current and damps oscillation on fast transitions.

### T7 S3 → Waveshare HAT+ (connect via jumper wires to HAT+ header)

```
T7 S3 V1.1          HAT+ 40-pin header
──────────          ───────────────────
GPIO12  ──────────→ SCLK  (pin 23)
GPIO11  ──────────→ MOSI  (pin 19)
GPIO10  ──────────→ CS    (pin 24)
GPIO13  ──────────→ DC    (BCM 25 / pin 22)
GPIO14  ──────────→ RST   (BCM 17 / pin 11)
GPIO15  ──────────→ BUSY  (BCM 24 / pin 18)
MOSFET drain ─────→ 3.3V  (pin 1)
GND     ──────────→ GND   (pin 6)
```

The 7.3" e-paper panel connects to the HAT+ via its ribbon cable as shipped.

### T7 S3 → MicroSD Module

```
T7 S3 V1.1          SD module
──────────          ─────────
GPIO5   ──────────→ MOSI
GPIO6   ──────────→ SCK
GPIO7   ──────────→ MISO
GPIO4   ──────────→ CS
MOSFET drain ─────→ VCC (3.3 V)
GND     ──────────→ GND
```

### Album Switch Button

```
T7 S3 V1.1
──────────
GPIO1 ──── [button] ──── GND
```

GPIO1 uses the ESP32-S3 internal pull-up. No external resistor required.
Button active LOW: pressed = GPIO1 reads LOW.

### Battery

Connect the 3.7 V LiPo to the T7 S3's JST battery connector. The T7 S3 includes
an onboard charger; charge via the USB-C port. GPIO2 reads battery voltage through
the onboard /2 voltage divider.

---

## Album Switching

The device supports two independent photo albums (A and B), each stored in its own
SD card directory.

### Switching Sequence

To switch the active album:

1. **Hold** the button for ≥ 2.5 seconds, then release.
2. **Press** the button once within 3 seconds.
3. **Press** the button a second time within 3 seconds.

Only then does the album switch. Any interrupted or timed-out sequence is silently
cancelled with no effect.

### Timing Constants (in `e-paper-esp32-frame.ino`)

```cpp
#define BUTTON_DEBOUNCE_MS        50    // Debounce window (ms)
#define ALBUM_HOLD_DURATION_MS  2500    // Hold time to initiate (~2.5 s)
#define ALBUM_CONFIRM_WINDOW_MS 3000    // Window for each confirm press (3 s)
#define BUTTON_POLL_MS            10    // Polling interval (ms)
```

Adjust these to taste. The button check blocks setup for at most
`ALBUM_HOLD_DURATION_MS + 2 × ALBUM_CONFIRM_WINDOW_MS` ≈ **8.5 seconds** before
proceeding normally if no valid sequence is detected.

### Album State Persistence

The active album index is stored in NVS (Non-Volatile Storage / `Preferences`) under
the namespace `e-paper`, key `albumIndex` (0 = Album A, 1 = Album B).
This survives deep sleep, resets, power cycles, and firmware updates.

### Debug Output

The firmware logs album switch progress over serial (115200 baud):

```
[BTN] Button active at boot.
[BTN] Hold for 2.5 s to initiate album switch...
[BTN] Hold detected! Release button to continue.
[BTN] Awaiting confirmation press 1/2 (within 3 s)...
[BTN] Confirmation press 1/2 received.
[BTN] Awaiting confirmation press 2/2 (within 3 s)...
[BTN] Confirmation press 2/2 received.
[BTN] *** ALBUM SWITCHED: A → B ***
```

---

## SD Card Setup

Format the SD card as FAT32. Create the following directory structure:

```
/
├── setup.json          ← WiFi credentials (see below)
├── albumA/
│   ├── info.txt        ← any content; change this to trigger a file rescan
│   ├── 001_15.06_photo1.bmp
│   ├── 002_16.06_photo2.bmp
│   └── ...             (up to 25 images, 800×480, 24-bit BMP)
└── albumB/
    ├── info.txt
    ├── 001_15.06_image1.bmp
    └── ...
```

**`setup.json`** (place in SD root):
```json
{
    "ssid": "YourNetworkName",
    "password": "YourPassword"
}
```

**File naming for date-based display:**  
Include a date in `DD.MM` format anywhere in the filename to show that image on
that specific calendar day (e.g. `042_25.12_christmas.bmp` → shows on 25 December).
Files without a date tag are shown sequentially on all other days.

**`info.txt`:** Any text file. The firmware uses its content as a change-detection
hash. Update this file whenever you add or remove images to trigger a rescan.

---

## Building & Flashing

### Arduino IDE Setup

1. Install Arduino IDE 2.x.
2. Add ESP32 board package via **File → Preferences → Additional Boards Manager URLs**:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Install **esp32 by Espressif Systems** via Board Manager.
4. Install the **ArduinoJson** library via Library Manager (≥ v6).

### Board Configuration

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| PSRAM | OPI PSRAM |
| Flash Size | 16 MB |
| Partition Scheme | Default 16 MB |
| USB Mode | Hardware CDC and JTAG |
| CPU Frequency | 240 MHz (reduced to 80 MHz at runtime) |
| Upload Speed | 921600 |

The `.vscode/arduino.json` already encodes these settings. Update the `port` field
to match your system's COM port.

### Upload

1. Hold the BOOT button on the T7 S3 while pressing RST to enter download mode.
2. Select the correct COM port in Arduino IDE.
3. Click Upload.
4. Press RST to reboot into the new firmware.

---

## Image Conversion

Use the included BMP Converter tool to prepare images:

1. Navigate to `/bmpConverter/build/exe.win-amd64-3.11/`.
2. Run `converter.exe`.
3. Load images (JPG, PNG, BMP).
4. Adjust crop/rotation/scale to fill the 800×480 frame (red rectangle).
5. Assign dates if desired.
6. Click **Export** and choose the appropriate album directory on your SD card.

The tool exports 24-bit BMP files at 800×480 pixels with correct filenames and
generates `info.txt` automatically.

To run from source:
```sh
cd bmpConverter
pip install -r requirements.txt
python converter.py
```

---

## Power Budget

| State | Current | Notes |
|---|---|---|
| Deep sleep (typical) | ~20 µA | ESP32-S3 RTC running, MOSFET off |
| Display refresh (active) | ~35–60 mA | CPU 80 MHz, SPI, WiFi |
| WiFi connect + NTP | +80–130 mA | Added on top of base active |
| Display writing (peak) | ~100 mA | During SPI data stream |

**Estimated battery life (1200 mAh, one refresh per day):**
- Active phase ≈ 60–90 seconds per day → ≈ 1.4–2.1 mAh/day
- Sleep phase ≈ 86310 seconds/day @ 20 µA → ≈ 0.48 mAh/day
- **Total ≈ 2–2.6 mAh/day → ~18–24 months per charge**

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| Display never updates | MOSFET not conducting | Check 100 kΩ pull-up; verify GPIO16 goes LOW in setup |
| SD mount fails | SD wiring or CS conflict | Verify SD is on SPI3 (GPIO4–7), not sharing SPI2 with display |
| Album switch not responding | Button wiring | Confirm button connects GPIO1 to GND; check serial for `[BTN]` messages |
| Image colors wrong | Wrong DISPLAY_TYPE define | Ensure `#define DISPLAY_TYPE_E` is active in `epd7in3combined.h` |
| EPD init fails | RST/BUSY wiring | Check GPIO14=RST, GPIO15=BUSY match HAT+ header pins |
| No WiFi | setup.json missing or wrong path | Place setup.json in SD **root**, not in albumA/B |

---

## License

MIT License

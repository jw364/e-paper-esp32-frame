# E-Paper ESP32 Picture Frame

A battery-powered digital picture frame built around the Waveshare 7.3" Spectra 6 full-color e-paper display. The frame wakes once a day, fetches the correct time over WiFi, renders the next photo from an SD card using Floyd–Steinberg dithering, updates the display, and returns to deep sleep — all in under 90 seconds. The e-ink panel holds the image indefinitely with zero power draw until the next update.

![Frame front](images/e-paper-esp32-frame.jpg?raw=true)
![Frame back](images/e-paper-esp32-frame-backside.jpg?raw=true)

---

## Table of Contents

- [Features](#features)
- [Hardware](#hardware)
- [How It Works](#how-it-works)
- [Wiring](#wiring)
  - [Full System Diagram](#full-system-diagram)
  - [MOSFET Power Switch Circuit](#mosfet-power-switch-circuit)
  - [Display HAT+ Connection](#display-hat-connection)
  - [SD Card Connection](#sd-card-connection)
  - [Album Button](#album-button)
  - [Battery](#battery)
- [Pin Reference](#pin-reference)
- [SD Card Setup](#sd-card-setup)
- [Album Switching](#album-switching)
- [Wokwi Simulation](#wokwi-simulation)
- [Image Conversion](#image-conversion)
- [Build & Flash](#build--flash)
- [Power Budget](#power-budget)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Features

- **6-color e-ink display** — Waveshare 7.3" Spectra 6 (Black, White, Red, Yellow, Blue, Green) at 800×480
- **Floyd–Steinberg dithering** — converts full-color photos to the 6-color palette with smooth gradients and no banding
- **Date-aware scheduling** — assign specific images to specific calendar dates; fall back to sequential rotation on undated days
- **Two switchable albums** — hold ≥2 s then press twice within a 5-second window to switch between Album A and Album B; album selection persists across power cycles
- **Ultra-low power** — ~20 µA deep sleep; estimated 12–18 months on a 1000 mAh LiPo with one refresh per day
- **MOSFET power gating** — AO3401 P-channel MOSFET cuts power to the display HAT+ and SD card during sleep
- **Battery monitor** — low-battery indicator in the image corner; indefinite sleep on critical voltage
- **WiFi time sync** — NTP-synchronized scheduling; graceful fallback if WiFi is unavailable
- **Included BMP converter** — Windows GUI tool to crop, rotate, and export photos to the correct format

---

## Hardware

| Component | Specification |
|---|---|
| [DFRobot FireBeetle 2 ESP32-E](https://www.dfrobot.com/product-2195.html) | ESP32, 4 MB Flash, onboard LiPo charger |
| [Waveshare 7.3" E-Paper HAT (E)](https://www.waveshare.com/product/displays/e-paper/epaper-1/7.3inch-e-paper-hat-e.htm) | Spectra 6 E6 panel, 800×480, 6-color, SPI, includes HAT+ driver board |
| MicroSD card module | 3.3 V SPI type |
| AO3401 P-channel MOSFET | SOT-23 or through-hole breakout |
| 10 kΩ resistor | MOSFET gate series resistor |
| 100 kΩ resistor | MOSFET gate pull-up |
| Momentary push button | Album switch |
| 3.7 V 1000 mAh LiPo battery | With JST-PH 2-pin connector to match FireBeetle 2 |

> **Important — GPIO restrictions on ESP32:**
> GPIO 6–11 are internally connected to the SPI flash chip and **cannot be used** for any other purpose. The pin assignments in this project deliberately avoid all of those pins.

---

## How It Works

```
Boot / Timer wakeup
       │
       ▼
Release GPIO hold → drive MOSFET LOW (GPIO21) → power on peripherals
       │
       ▼
Check album button (up to ~8 s if held; instant skip otherwise)
       │
       ▼
Mount SD card → connect WiFi → sync NTP time
       │
       ▼
Scan album directory → find next image by date or index
       │
       ▼
Stream BMP from SD → Floyd–Steinberg dither → send pixels to display
       │
       ▼
TurnOnDisplay (triggers e-ink refresh, ~30–40 s) → Sleep display
       │
       ▼
Drive MOSFET HIGH (GPIO21) → power off peripherals
gpio_hold_en → deep sleep until next scheduled time (target: 10:00 AM daily)
```

The display retains the image without any power after the firmware calls `Sleep()`. The MOSFET cuts the 3.3 V rail to both the HAT+ and the SD card module during deep sleep, ensuring the only quiescent draw is the ESP32 RTC (~20 µA).

---

## Wiring

### Full System Diagram

```
                    ┌──────────────────────────────────────────┐
                    │        FireBeetle 2 ESP32-E              │
                    │                                          │
  LiPo ─────────── │ BAT    GPIO36 ──── [100kΩ]─┬─[100kΩ]─── │ ─── VBAT
                    │                            └──────────── │ (battery divider)
                    │        GPIO21 ──────────────────────────── MOSFET gate
                    │        GPIO22 ──────────────────────────── Album button ── GND
                    │                                          │
                    │  VSPI  GPIO5  (CS)                       │
                    │        GPIO23 (MOSI) ────────────────────── HAT+ display SPI
                    │        GPIO18 (SCLK)                     │
                    │        GPIO27 (DC)                       │
                    │        GPIO26 (RST)                      │
                    │        GPIO25 (BUSY)                     │
                    │                                          │
                    │  HSPI  GPIO33 (CS)                       │
                    │        GPIO13 (MOSI) ────────────────────── SD card module
                    │        GPIO14 (SCLK)                     │
                    │        GPIO4  (MISO)                     │
                    │                                          │
                    │        3.3V ─── MOSFET source            │
                    │        GND  ─── HAT+ GND, SD GND        │
                    └──────────────────────────────────────────┘

  MOSFET drain ──── HAT+ 3.3V pin
               └─── SD module VCC
```

---

### MOSFET Power Switch Circuit

The AO3401 is a P-channel MOSFET used as a high-side switch. When its gate is pulled
low (toward GND), it conducts and powers the peripherals. When its gate is at source
potential (3.3 V), it is off.

```
   FireBeetle 3.3 V ───────────────────────────┬──────── AO3401  S (source)
                                               │
                                             [100 kΩ]  ← pull-up: holds gate HIGH
                                               │         (MOSFET off) during boot
   GPIO21 ──────────── [10 kΩ] ───────────────┤──────── AO3401  G (gate)
                         series                            ↓ Vgs ≈ −3.3 V when GPIO21 LOW
                                                           → MOSFET conducts
                                                    AO3401  D (drain)
                                                           │
                           ┌───────────────────────────────┤
                           │                               │
                    HAT+ 3.3 V pin                 SD module VCC
```

**100 kΩ pull-up purpose:** GPIO21 is high-impedance for a brief moment during boot
before the firmware configures it as an output. Without this resistor, the gate would
float and the MOSFET could conduct unpredictably. The pull-up biases the gate to source
potential (Vgs = 0) by default, keeping the MOSFET off until the firmware explicitly
drives GPIO21 LOW.

**10 kΩ series resistor purpose:** Limits the peak gate-charge current on transitions
and suppresses ringing on fast edges.

---

### Display HAT+ Connection

Connect jumper wires from the FireBeetle 2 to the Waveshare HAT+ 40-pin header. The
e-paper panel itself attaches to the HAT+ via its FPC ribbon cable — no additional
wiring needed between panel and HAT+.

```
FireBeetle 2       HAT+ 40-pin header (BCM numbering)
────────────        ─────────────────────────────────
GPIO18   ────────→ SCLK   pin 23  (BCM 11)
GPIO23   ────────→ MOSI   pin 19  (BCM 10)
GPIO5    ────────→ CE0    pin 24  (BCM  8)   ← SPI chip select
GPIO27   ────────→ DC     pin 22  (BCM 25)
GPIO26   ────────→ RST    pin 11  (BCM 17)
GPIO25   ────────→ BUSY   pin 18  (BCM 24)
MOSFET D ────────→ 3.3V   pin  1
GND      ────────→ GND    pin  6
```

---

### SD Card Connection

The SD card uses a completely separate SPI bus (HSPI) from the display (VSPI). This
eliminates any pin-sharing conflict and allows both devices to operate independently.

```
FireBeetle 2       SD module
────────────        ─────────
GPIO13   ────────→ MOSI
GPIO14   ────────→ SCK
GPIO4    ────────→ MISO
GPIO33   ────────→ CS
MOSFET D ────────→ VCC  (3.3 V)
GND      ────────→ GND
```

> **Note on GPIO4 (MISO):** The HSPI peripheral default MISO is GPIO12, but GPIO12 is
> a flash-voltage strapping pin on ESP32 that **must remain LOW at boot** for 3.3 V
> flash operation. Remapping MISO to GPIO4 avoids this constraint entirely.

---

### Album Button

```
GPIO22 ──── [ button ] ──── GND
```

GPIO22 uses the ESP32 internal pull-up resistor. No external components required.
The button reads LOW when pressed.

---

### Battery

Connect a 3.7 V LiPo with a JST-PH 2-pin connector to the battery port on the
FireBeetle 2. The board includes an onboard charging circuit; recharge via USB-C.
Do not reverse the connector polarity.

GPIO36 reads battery voltage through the FireBeetle 2's onboard resistor divider (×½).
The firmware multiplies the ADC reading by 2 to recover actual battery voltage.

> **Board revision note:** The battery ADC pin may vary across FireBeetle 2 board
> revisions. If battery readings are always 0.0 V, try GPIO34 or GPIO35 by changing
> `BAT_ADC_PIN` in `e-paper-esp32-frame.ino`. Check the schematic for your board
> revision on the DFRobot wiki.

| Voltage | Meaning |
|---|---|
| > 3.9 V | Healthy |
| 3.5 – 3.9 V | Normal discharge range |
| 3.3 – 3.5 V | Low — red indicator shown in image corner |
| < 3.1 V | Critical — device enters indefinite sleep |

---

## Pin Reference

| GPIO | Function | SPI Bus | Direction |
|---:|---|---|---|
| 5 | Display CS | VSPI | Output |
| 18 | Display SCLK | VSPI | Output |
| 23 | Display MOSI | VSPI | Output |
| 27 | Display DC | — | Output |
| 26 | Display RST | — | Output |
| 25 | Display BUSY | — | Input |
| 33 | SD CS | HSPI | Output |
| 13 | SD MOSI | HSPI | Output |
| 14 | SD SCLK | HSPI | Output |
| 4 | SD MISO | HSPI | Input |
| 21 | MOSFET gate | — | Output |
| 36 | Battery ADC | — | Analog input |
| 22 | Album button | — | Input (pull-up) |
| 6–11 | Internal flash | — | **Reserved — do not use** |
| 0 | BOOT / strapping | — | **Reserved — do not use** |
| 1 | UART0 TX | — | **Reserved — do not use** |
| 3 | UART0 RX | — | **Reserved — do not use** |
| 12 | Flash strapping | — | **Do not drive HIGH after boot** |

---

## SD Card Setup

Format the card as **FAT32**. The following structure is required:

```
SD root/
├── setup.json              ← WiFi credentials
├── albumA/
│   ├── info.txt            ← change detection file (update when adding/removing images)
│   ├── 001_beach.bmp
│   ├── 002_25.12_christmas.bmp
│   └── ...                 (800×480 px, 24-bit BMP, up to 25 images)
└── albumB/
    ├── info.txt
    ├── 001_mountains.bmp
    └── ...
```

### setup.json

```json
{
    "ssid": "YourNetworkName",
    "password": "YourPassword"
}
```

### Image filenames

Include a date string in `DD.MM` format anywhere in the filename to pin that image
to a specific calendar day. The firmware scans for the current date each morning and
shows the matching image if one exists.

```
042_25.12_christmas_morning.bmp   → displayed on 25 December every year
018_01.01_new_year.bmp            → displayed on 1 January every year
037_holiday_beach.bmp             → no date; shown on rotation on undated days
```

### info.txt

The firmware compares `info.txt` content to a stored checksum. Changing anything in
this file (even a single character) triggers a full directory rescan and resets the
sequential image index for that album. Update it after every batch of image changes.

### Auto-generated files

The firmware writes `/fileStringA.txt` and `/fileStringB.txt` to the SD card root as
its internal file-list cache. Do not edit or delete these manually.

---

## Album Switching

The device maintains two independent photo albums. Each album stores its own image
list, current position, and file cache in NVS separately.

### Switching sequence

The button requires a deliberate multi-step sequence to prevent accidental switches:

```
Step 1  Hold button > 2 s           →  release
Step 2  5-second verification window begins
Step 3  Press button once            →  release  ┐ both presses must fall
Step 4  Press button a second time   →  album switches  ┘ within the 5 s window
```

Any step that times out or is skipped cancels the operation silently with no effect.
A short press (< 2 s) is always ignored.

**Edge cases that do NOT switch albums:**
- Hold shorter than 2 s
- Only one press during the 5-second window
- Two presses that both arrive after the 5-second window expires
- Random button activity that does not follow the exact sequence

### Serial log output (115200 baud)

Successful switch:
```
[BTN] Button active — waiting to detect hold...
[BTN] Hold detected! Duration: 2053 ms.
[BTN] Release button to start verification window...
[BTN] Verification window started — press button TWICE within 5 s.
[BTN] First verification press detected (1234 ms into window).
[BTN] Second verification press detected (2891 ms into window).
[BTN] *** ALBUM SWITCHED: A → B ***
```

Hold too short:
```
[BTN] Button active — waiting to detect hold...
[BTN] Hold too short (832 ms — need 2000 ms). Cancelled.
```

Only one press in the window:
```
[BTN] Verification window started — press button TWICE within 5 s.
[BTN] First verification press detected (1500 ms into window).
[BTN] Verification timeout — no press 2 received. Cancelled.
```

### Timing constants

All timing values are `#define` constants near the top of `e-paper-esp32-frame.ino`:

```cpp
#define BUTTON_DEBOUNCE_MS        50    // Stable-state time for a valid edge (ms)
#define ALBUM_HOLD_DURATION_MS  2000    // Minimum hold to begin the sequence (ms)
#define ALBUM_CONFIRM_WINDOW_MS 5000    // Shared window for BOTH confirmation presses (ms)
#define BUTTON_POLL_MS            10    // Polling interval during button checks (ms)
```

### Persistence

The active album index is stored in ESP32 NVS (flash) under namespace `e-paper`,
key `albumIndex`. It survives deep sleep, hard resets, power disconnection, and
firmware reflashing (NVS is in a separate flash partition).

---

## Wokwi Simulation

The repository includes a complete [Wokwi](https://wokwi.com) simulation setup for
testing the firmware — including the album-switch button sequence — without physical
hardware.

### Files

| File | Purpose |
|---|---|
| `diagram.json` | Wokwi hardware layout: ESP32 DevKit + pushbutton + SD card + BUSY pull-up |
| `wokwi.toml` | Points Wokwi to the compiled ELF/BIN in `build/` |
| `libraries.txt` | ArduinoJson dependency for Wokwi GitHub Actions CI |
| `sdcard/` | SD card filesystem root served to the simulation |
| `sdcard/setup.json` | WiFi credentials — pre-configured for Wokwi's `Wokwi-GUEST` AP |
| `sdcard/albumA/test.bmp` | Minimal 4×4-pixel BMP used as test image |

### Setup

1. Install the **Arduino** extension (Microsoft) and **Wokwi Simulator** extension in VS Code.
2. Install the `esp32:esp32` board package via Boards Manager.
3. Install the `ArduinoJson` library (≥ v6) via Library Manager.
4. Compile the sketch with **Ctrl+Alt+B**. Output lands in `build/`.
5. Press **F1 → Wokwi: Start Simulator** (or `Ctrl+Shift+P`).

> The BUSY pin (GPIO25) is wired to 3.3 V via a 10 kΩ resistor in `diagram.json`.
> This simulates "display always ready", so all `EPD_7IN3F_BusyHigh()` calls return
> immediately. A 30-second software timeout is also present as a safety net.

### Triggering an album switch in the simulation

Click the **green push button** in the Wokwi canvas:

| Action | How to do it in Wokwi |
|---|---|
| Hold button | Click and **hold** the mouse button on the green button |
| Release | Release the mouse button |
| Short press | Click and release quickly (< 2 s) |

**To perform a successful album switch:**

1. At simulation start (or after reset), **click and hold** the green button for **more than 2 seconds**.
2. **Release** the button.
3. Within 5 seconds, **click and release** the button once.
4. Within the same 5-second window, **click and release** the button a second time.
5. Watch the Serial Monitor for `[BTN] *** ALBUM SWITCHED: A → B ***`.

**To verify cancellation cases:**

| Test | How |
|---|---|
| Hold < 2 s | Click and release quickly; observe "Hold too short" |
| Only one press | Hold > 2 s, release, press once, do nothing — observe timeout |
| Two presses after window | Hold > 2 s, release, wait > 5 s, then press twice — observe timeout |

The Serial Monitor output (always shown) will log every step of the state machine.

---

## Image Conversion

The `/bmpConverter` directory contains a Windows GUI tool to convert photos into the
correct format (800×480, 24-bit BMP).

### Running the prebuilt executable

1. Open `bmpConverter/build/exe.win-amd64-3.11/converter.exe`
2. Click **Bilder Laden** to load one or more images (JPG, PNG, BMP)
3. Use the arrow keys or buttons to pan, and `+`/`−` to zoom within the 800×480 frame
4. Use the rotation buttons to rotate 90°
5. Optionally assign a calendar date using the date picker
6. Click **Bilder Exportieren**, choose the album directory on your SD card

The tool generates correctly named BMP files and creates `info.txt` automatically.

### Running from source

```sh
cd bmpConverter
pip install -r requirements.txt
python converter.py
```

Requires Python 3.9+ and Pillow.

---

## Build & Flash

### 1. Install Arduino IDE 2.x

Download from [arduino.cc](https://www.arduino.cc/en/software).

### 2. Add the ESP32 board package

Open **File → Preferences** and add to *Additional Boards Manager URLs*:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then open **Tools → Board → Boards Manager**, search for `esp32`, and install
**esp32 by Espressif Systems** (version 3.x recommended).

### 3. Install libraries

Open **Tools → Manage Libraries** and install:

| Library | Minimum version |
|---|---|
| ArduinoJson | 6.0 |

### 4. Board settings

Select **Tools → Board → ESP32 Arduino → DFRobot FireBeetle-2-ESP32E** and configure:

| Setting | Value |
|---|---|
| PSRAM | Disabled |
| Flash Size | 4 MB (32 Mb) |
| Partition Scheme | Default 4MB with spiffs |
| CPU Frequency | 240 MHz |
| Upload Speed | 921600 |

These settings are also stored in `.vscode/arduino.json`. If using VS Code with the
Arduino extension, update the `port` field to match your COM port.

### 5. Upload

1. Connect the FireBeetle 2 via USB-C.
2. The board uses an auto-reset circuit — simply click **Upload** in the IDE.
3. If upload fails, hold the **BOOT** button while pressing **RST** to force download
   mode, then retry.

---

## Power Budget

| State | Avg. current | Duration (per day) |
|---|---|---|
| Deep sleep (ESP32 RTC) | ~20 µA | ~86,310 s (23 h 58 m) |
| Boot + WiFi + NTP | ~150 mA peak | ~15–20 s |
| Display rendering + refresh | ~60–80 mA | ~40–60 s |
| MOSFET leakage during sleep | < 1 µA | — |

**Estimated daily consumption:**

```
Sleep:    86,310 s × 0.020 mA  = 0.48 mAh
Active:      90 s × 80 mA avg  = 2.00 mAh
                                 ─────────
Total per day ≈ 2.5 mAh
```

A 1000 mAh LiPo provides approximately **400 days** (~13 months) per charge under
these conditions. Real-world life varies with WiFi connection time and temperature.

To improve battery life further:
- Move the frame closer to the router to reduce WiFi association time
- The firmware already reduces CPU to 80 MHz via `setCpuFrequencyMhz(80)` during the active window
- Consider disabling NTP sync after a successful time fetch and relying on drift-corrected RTC

---

## Troubleshooting

| Symptom | Likely cause | Resolution |
|---|---|---|
| Display never updates, stays blank | MOSFET not conducting | Verify GPIO21 goes LOW in `setup()`. Check 100 kΩ pull-up is from gate to Source (3.3 V), not to GND. |
| SD mount fails on every boot | SPI bus or CS wiring | Confirm SD uses GPIO14/13/4/33. Check all wires with a multimeter. |
| Display initializes but image is garbled | Incorrect SPI pins | Verify GPIO18/23/5/27/26/25 match HAT+ header pins exactly. |
| Image colors look wrong (red/blue swapped) | Wrong `DISPLAY_TYPE` | Confirm `#define DISPLAY_TYPE_E` is active in `epd7in3combined.h` (not `DISPLAY_TYPE_F`). |
| Album button does nothing | Wiring or wrong GPIO | Confirm button connects GPIO22 to GND. Open serial monitor — `[BTN]` lines should appear when button is held. |
| Album switch cancels before completing | Timing too tight | Increase `ALBUM_CONFIRM_WINDOW_MS` in the sketch. |
| No WiFi connection | Credentials or range | Check `setup.json` is in the SD **root** (not inside albumA/B). Verify SSID/password. |
| Frame wakes but shows nothing | Empty album directory | Confirm `/albumA/` or `/albumB/` contains `.bmp` files and a valid `info.txt`. |
| Battery reading is always 0 V | Wrong ADC pin | See battery note above — try GPIO34 or GPIO35 for `BAT_ADC_PIN` depending on board revision. |
| Upload fails in Arduino IDE | Port or driver issue | Ensure CP2102 driver is installed. Try a different USB-C cable. |
| GPIO6–11 compile warning | Misconfiguration | Never use GPIO6–11; they are reserved for the internal SPI flash on ESP32. |

---

## Contributing

Pull requests welcome. Please open an issue first for significant changes.

---

## License

MIT License

/**
 * epdif.h — EPD SPI interface for DFRobot FireBeetle 2 ESP32-E
 * Display: Waveshare 7.3" Spectra 6 (E6) via HAT+ Driver Board
 *
 * SPI bus: VSPI (SPI2 peripheral) — avoids GPIO 6–11 (internal flash bus)
 *
 * Wiring (FireBeetle 2 → HAT+ header):
 *   GPIO18 → CLK   (SCLK)
 *   GPIO23 → DIN   (MOSI)
 *   GPIO5  → CS
 *   GPIO27 → DC
 *   GPIO26 → RST
 *   GPIO25 → BUSY
 */

#ifndef EPDIF_H
#define EPDIF_H

#include <Arduino.h>

// ── Display SPI (VSPI / SPI2) ─────────────────────────────────────────────────
#define EPD_SCLK    18    // VSPI SCK  (GPIO 6–11 are reserved for flash on ESP32)
#define EPD_MOSI    23    // VSPI MOSI
#define CS_PIN       5    // VSPI CS0  (strapping pin — HIGH at power-on is fine)
#define DC_PIN      27
#define RST_PIN     26
#define BUSY_PIN    25

class EpdIf {
public:
    EpdIf(void);
    ~EpdIf(void);

    static int  IfInit(void);
    static void DigitalWrite(int pin, int value);
    static int  DigitalRead(int pin);
    static void DelayMs(unsigned int delaytime);
    static void SpiTransfer(unsigned char data);
};

#endif

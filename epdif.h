/**
 * epdif.h — EPD SPI interface for LILYGO T7 S3 V1.1 (ESP32-S3)
 * Display: Waveshare 7.3" Spectra 6 (E6) via HAT+ Driver Board
 *
 * SPI bus: FSPI (SPI2 peripheral, bus index 0 on ESP32-S3)
 *
 * Wiring (T7 S3 → HAT+ header):
 *   GPIO12 → CLK   (SCLK)
 *   GPIO11 → DIN   (MOSI)
 *   GPIO10 → CS
 *   GPIO13 → DC
 *   GPIO14 → RST
 *   GPIO15 → BUSY
 */

#ifndef EPDIF_H
#define EPDIF_H

#include <arduino.h>

// ── Display SPI (FSPI / SPI2) ─────────────────────────────────────────────────
#define EPD_SCLK    12    // SPI2 default SCK  on ESP32-S3
#define EPD_MOSI    11    // SPI2 default MOSI on ESP32-S3
#define CS_PIN      10    // SPI2 default CS0  on ESP32-S3
#define DC_PIN      13
#define RST_PIN     14
#define BUSY_PIN    15

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

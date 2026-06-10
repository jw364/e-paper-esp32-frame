/**
 * epdif.h — EPD hardware interface for LILYGO T7-S3 V1.1
 *
 * Wiring (T7-S3 GPIO → HAT+ Standard Driver Board pin):
 *   GPIO 8  → RST
 *   GPIO 9  → DC
 *   GPIO 10 → CS
 *   GPIO 7  → BUSY
 *   GPIO 11 → DIN (MOSI)
 *   GPIO 12 → CLK (SCK)
 *   3.3V    → VCC
 *   GND     → GND
 */

#ifndef EPDIF_H
#define EPDIF_H

#include <arduino.h>

#define EPD_RST_PIN   8
#define EPD_DC_PIN    9
#define EPD_CS_PIN    10
#define EPD_BUSY_PIN  7
#define EPD_MOSI_PIN  11
#define EPD_SCK_PIN   12

#define RST_PIN   EPD_RST_PIN
#define DC_PIN    EPD_DC_PIN
#define CS_PIN    EPD_CS_PIN
#define BUSY_PIN  EPD_BUSY_PIN

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

/**
 * epdif.cpp — EPD SPI interface implementation for ESP32-S3
 *
 * Uses FSPI (SPI2, bus index 0) which is dedicated to the e-paper display.
 * The SD card uses a separate HSPI (SPI3, bus index 1) bus to eliminate
 * the pin-sharing conflict present in the original FireBeetle design.
 */

#include "epdif.h"
#include <SPI.h>

// Fallback defines in case an older ESP32 Arduino core omits them.
#ifndef FSPI
#define FSPI 0
#endif

// FSPI = 0 on ESP32-S3 (SPI2 peripheral).
// Default pins: SCK=12, MOSI=11, MISO=13, CS=10 — matches our EPD_SCLK/EPD_MOSI.
SPIClass epd_spi(FSPI);

EpdIf::EpdIf() {}
EpdIf::~EpdIf() {}

void EpdIf::DigitalWrite(int pin, int value) {
    digitalWrite(pin, value);
}

int EpdIf::DigitalRead(int pin) {
    return digitalRead(pin);
}

void EpdIf::DelayMs(unsigned int delaytime) {
    delay(delaytime);
}

void EpdIf::SpiTransfer(unsigned char data) {
    digitalWrite(CS_PIN, LOW);
    epd_spi.transfer(data);
    digitalWrite(CS_PIN, HIGH);
}

int EpdIf::IfInit(void) {
    pinMode(CS_PIN,   OUTPUT);
    pinMode(RST_PIN,  OUTPUT);
    pinMode(DC_PIN,   OUTPUT);
    pinMode(BUSY_PIN, INPUT);
    digitalWrite(CS_PIN, HIGH);  // Deselect display

    // Begin FSPI with explicit pin assignment; no MISO needed (display is write-only)
    epd_spi.begin(EPD_SCLK, /*MISO=*/-1, EPD_MOSI, /*CS=*/-1);
    // 4 MHz is conservative and reliable over HAT+ wiring; display supports up to ~20 MHz
    epd_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    return 0;
}

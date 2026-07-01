/**
 * epdif.cpp — EPD SPI interface implementation for FireBeetle 2 ESP32-E
 *
 * Uses VSPI (SPI2 on original ESP32) dedicated to the e-paper display.
 * GPIO 6–11 are reserved for internal flash on ESP32 and cannot be used.
 * The SD card uses a separate HSPI bus to keep bus loads independent.
 */

#include "epdif.h"
#include <SPI.h>

// Fallback for older Arduino-ESP32 cores that may not define VSPI.
// On ESP32 (original) with core 2.x / 3.x, VSPI = 3 (SPI2 peripheral).
#ifndef VSPI
#define VSPI 3
#endif

// VSPI (SPI2 peripheral on original ESP32).
// Pins are set explicitly in IfInit() via begin(sck, miso, mosi, cs).
SPIClass epd_spi(VSPI);

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

    // Begin VSPI with explicit pin assignment; no MISO needed (display is write-only)
    epd_spi.begin(EPD_SCLK, /*MISO=*/-1, EPD_MOSI, /*CS=*/-1);
    // 4 MHz is conservative and reliable over HAT+ wiring; display supports up to ~20 MHz
    epd_spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    return 0;
}

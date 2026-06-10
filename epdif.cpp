/**
 * epdif.cpp — EPD hardware interface for LILYGO T7-S3 V1.1 (ESP32-S3)
 */

#include "epdif.h"
#include <SPI.h>

// Use SPI2 (FSPI) — ESP32-S3's default user SPI bus
static SPIClass epd_spi(FSPI);

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
    // begin(SCK, MISO, MOSI, SS) — no MISO needed for write-only EPD
    epd_spi.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, -1);
    epd_spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    return 0;
}

/**
 * epd3in6e.cpp — Driver for Waveshare 3.6inch E Ink Spectra 6 (E6) display
 *
 * Init sequence is identical to the 7.3" E variant of the Spectra 6 family
 * (same UC8253-series controller) except command 0x61 uses 600×400 resolution.
 */

#include "epd3in6e.h"

Epd::Epd() {}
Epd::~Epd() {}

int Epd::Init(void) {
    if (IfInit() != 0) return -1;

    Reset();
    DelayMs(20);
    BusyHigh();

    SendCommand(0xAA);          // CMDH — unlock command sequence
    SendData(0x49); SendData(0x55); SendData(0x20);
    SendData(0x08); SendData(0x09); SendData(0x18);

    SendCommand(0x01);          // Power Setting
    SendData(0x3F); SendData(0x00); SendData(0x32);
    SendData(0x2A); SendData(0x0E); SendData(0x2A);

    SendCommand(0x00);          // Panel Setting
    SendData(0x5F); SendData(0x69);

    SendCommand(0x03);
    SendData(0x00); SendData(0x54); SendData(0x00); SendData(0x44);

    SendCommand(0x05);          // Booster Soft Start
    SendData(0x40); SendData(0x1F); SendData(0x1F); SendData(0x2C);

    SendCommand(0x06);
    SendData(0x6F); SendData(0x1F); SendData(0x1F); SendData(0x22);

    SendCommand(0x08);
    SendData(0x6F); SendData(0x1F); SendData(0x1F); SendData(0x22);

    SendCommand(0x13);          // IPC
    SendData(0x00); SendData(0x04);

    SendCommand(0x30);          // PLL Setting
    SendData(0x3C);

    SendCommand(0x41);          // Temperature Sensor Enable
    SendData(0x00);

    SendCommand(0x50);          // Vcom and Data Interval
    SendData(0x3F);

    SendCommand(0x60);          // TCON Setting
    SendData(0x02); SendData(0x00);

    SendCommand(0x61);          // Resolution Setting — 600 × 400
    SendData(0x02);             //  600 = 0x0258  high byte
    SendData(0x58);             //                 low byte
    SendData(0x01);             //  400 = 0x0190  high byte
    SendData(0x90);             //                 low byte

    SendCommand(0x82);          // Vcom DC Setting
    SendData(0x1E);

    SendCommand(0x84);
    SendData(0x00);

    SendCommand(0x86);          // AGID
    SendData(0x00);

    SendCommand(0xE3);          // Power Saving
    SendData(0x2F);

    SendCommand(0xE0);          // CCSET
    SendData(0x00);

    SendCommand(0xE6);          // TSSET
    SendData(0x00);

    return 0;
}

void Epd::SendCommand(unsigned char command) {
    DigitalWrite(DC_PIN, LOW);
    SpiTransfer(command);
}

void Epd::SendData(unsigned char data) {
    DigitalWrite(DC_PIN, HIGH);
    SpiTransfer(data);
}

void Epd::BusyHigh(void) {
    // BUSY pin is HIGH when panel is ready
    while (!DigitalRead(BUSY_PIN)) {
        DelayMs(1);
    }
}

void Epd::Reset(void) {
    DigitalWrite(RST_PIN, HIGH); DelayMs(20);
    DigitalWrite(RST_PIN, LOW);  DelayMs(2);
    DigitalWrite(RST_PIN, HIGH); DelayMs(20);
}

void Epd::TurnOnDisplay(void) {
    SendCommand(0x04);  // Power ON
    DelayMs(20);
    BusyHigh();

    SendCommand(0x12);  // Display Refresh
    SendData(0x00);
    DelayMs(100);
    BusyHigh();

    SendCommand(0x02);  // Power OFF
    SendData(0x00);
    BusyHigh();
}

// Fill (rows × cols) pixels with a solid color.
// Must be called after SendCommand(0x10) to stream into the current data frame.
void Epd::DrawBlank(UWORD rows, UWORD cols, UBYTE color) {
    UBYTE packed = (color << 4) | color;
    for (int i = 0; i < cols / 2; i++) {
        for (int j = 0; j < rows; j++) {
            SendData(packed);
        }
    }
}

void Epd::Clear(UBYTE color) {
    SendCommand(0x10);
    UBYTE packed = (color << 4) | color;
    for (int i = 0; i < EPD_WIDTH / 2; i++) {
        for (int j = 0; j < EPD_HEIGHT; j++) {
            SendData(packed);
        }
    }
    TurnOnDisplay();
}

void Epd::Sleep(void) {
    SendCommand(0x07);
    SendData(0xA5);
    DelayMs(10);
    DigitalWrite(RST_PIN, LOW);
}

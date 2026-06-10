/**
 * epd3in6e.h — Driver for Waveshare 3.6inch E Ink Spectra 6 (E6) display
 *              600×400 pixels, 6-color: Black / White / Yellow / Red / Blue / Green
 */

#ifndef __EPD_3IN6E_H__
#define __EPD_3IN6E_H__

#include "epdif.h"

#define EPD_WIDTH   600
#define EPD_HEIGHT  400

typedef unsigned int   UWORD;
typedef unsigned char  UBYTE;

// 4-bit color nibble codes sent over SPI (2 pixels packed per byte)
#define EPD_BLACK   0x0
#define EPD_WHITE   0x1
#define EPD_YELLOW  0x2
#define EPD_RED     0x3
#define EPD_BLUE    0x5   // 0x4 is reserved
#define EPD_GREEN   0x6

class Epd : EpdIf {
public:
    Epd();
    ~Epd();
    int  Init(void);
    void BusyHigh(void);
    void TurnOnDisplay(void);
    void Reset(void);
    void SendCommand(unsigned char command);
    void SendData(unsigned char data);
    void Sleep(void);
    void Clear(UBYTE color);
    void DrawBlank(UWORD rows, UWORD cols, UBYTE color);
};

#endif

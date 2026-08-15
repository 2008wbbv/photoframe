#include "ch422g.h"

#include <Arduino.h>
#include <Wire.h>

namespace ch422g {
namespace {

// Each of these is an I2C *device address*, not a register offset.
const uint8_t ADDR_MODE = 0x24;      // system / mode setting
const uint8_t ADDR_OUT = 0x38;       // write IO0..IO7
const uint8_t ADDR_IN = 0x26;        // read IO0..IO7
const uint8_t ADDR_OUT_UPPER = 0x23; // write OC0..OC3 (unused here)

const uint8_t MODE_IO_OUTPUT = 0x01; // IO0..IO7 push-pull output

uint8_t g_state = 0;

bool writeByte(uint8_t addr, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

}  // namespace

bool begin() {
  if (!writeByte(ADDR_MODE, MODE_IO_OUTPUT)) {
    return false;
  }

  // Touch and LCD out of reset, display on, SD selected, USB mux to USB.
  g_state = (1 << EXIO_TP_RST) | (1 << EXIO_DISP) | (1 << EXIO_LCD_RST);
  return writeByte(ADDR_OUT, g_state);
}

void write(uint8_t bit, bool high) {
  if (high) {
    g_state |= (uint8_t)(1 << bit);
  } else {
    g_state &= (uint8_t)~(1 << bit);
  }
  writeByte(ADDR_OUT, g_state);
}

void setDisplayEnabled(bool on) { write(EXIO_DISP, on); }

void resetPanel() {
  write(EXIO_LCD_RST, false);
  write(EXIO_TP_RST, false);
  delay(20);
  write(EXIO_LCD_RST, true);
  write(EXIO_TP_RST, true);
  delay(100);
}

}  // namespace ch422g

#include "bh1750.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace bh1750 {
namespace {

const uint8_t CMD_POWER_ON = 0x01;
const uint8_t CMD_RESET = 0x07;
const uint8_t CMD_CONT_HIRES = 0x10;  // 1 lx resolution, ~120 ms conversion

// Datasheet counts-to-lux divisor.
const float COUNTS_PER_LUX = 1.2f;

uint8_t g_addr = 0;
bool g_present = false;

bool ping(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool command(uint8_t addr, uint8_t cmd) {
  Wire.beginTransmission(addr);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

bool start(uint8_t addr) {
  if (!command(addr, CMD_POWER_ON)) return false;
  delay(10);
  command(addr, CMD_RESET);
  delay(10);
  if (!command(addr, CMD_CONT_HIRES)) return false;
  delay(180);  // let the first conversion complete
  return true;
}

}  // namespace

bool begin() {
  g_present = false;
  g_addr = 0;

  if (ping(BH1750_ADDR_HIGH) && start(BH1750_ADDR_HIGH)) {
    g_addr = BH1750_ADDR_HIGH;
    g_present = true;
    Serial.printf("[light] BH1750 found at 0x%02X\n", g_addr);
    return true;
  }

  // Nothing on 0x5C. Before giving up, check whether it is sitting on the
  // address the expander needs — that is the single most likely wiring mistake
  // with a stock GY-302 module.
  if (start(BH1750_ADDR_LOW)) {
    g_addr = BH1750_ADDR_LOW;
    g_present = true;
    Serial.println("[light] *** BH1750 responded on 0x23 ***");
    Serial.println("[light] 0x23 is also the CH422G expander (backlight, LCD");
    Serial.println("[light] reset, SD chip-select). Solder the GY-302's ADDR");
    Serial.println("[light] pin to 3V3 to move the sensor to 0x5C.");
    return true;
  }

  Serial.println("[light] no BH1750 found — auto-dimming disabled");
  return false;
}

bool present() { return g_present; }

uint8_t address() { return g_addr; }

float readLux() {
  if (!g_present) return -1.0f;

  if (Wire.requestFrom(g_addr, (uint8_t)2) != 2) return -1.0f;
  uint16_t raw = (uint16_t)Wire.read() << 8;
  raw |= Wire.read();
  return (float)raw / COUNTS_PER_LUX;
}

}  // namespace bh1750

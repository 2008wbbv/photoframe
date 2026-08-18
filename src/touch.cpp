#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace touch {
namespace {

// GT911 registers are 16-bit and big-endian on the wire.
const uint16_t REG_PRODUCT_ID = 0x8140;
const uint16_t REG_STATUS = 0x814E;
const uint16_t REG_POINT1 = 0x8150;

const uint8_t STATUS_READY = 0x80;
const uint8_t STATUS_COUNT_MASK = 0x0F;

uint8_t g_addr = 0;
bool g_present = false;

// Gesture state
bool g_down = false;
uint32_t g_downAt = 0;
int16_t g_downX = 0;
bool g_longFired = false;
uint32_t g_lastPoll = 0;

bool readBytes(uint16_t reg, uint8_t *out, size_t len) {
  Wire.beginTransmission(g_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission() != 0) return false;

  if (Wire.requestFrom(g_addr, (uint8_t)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) out[i] = Wire.read();
  return true;
}

bool writeByte(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(g_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool probe(uint8_t addr) {
  g_addr = addr;
  uint8_t id[4] = {0};
  if (!readBytes(REG_PRODUCT_ID, id, sizeof(id))) return false;
  // Genuine parts report an ASCII product id — "911", "911S" and similar.
  return id[0] == '9' && id[1] == '1' && id[2] == '1';
}

// Three outcomes, and the distinction matters: the GT911 only raises its ready
// flag when it has a *new* frame, roughly every 10 ms. Treating "no new frame"
// as "finger lifted" would invent a release — and therefore a tap — on any poll
// that happened to fall between frames.
enum class Frame : uint8_t { Stale, Up, Down };

Frame readFrame(int16_t *x, int16_t *y) {
  uint8_t status = 0;
  if (!readBytes(REG_STATUS, &status, 1)) return Frame::Stale;
  if (!(status & STATUS_READY)) return Frame::Stale;

  uint8_t count = status & STATUS_COUNT_MASK;
  Frame result = Frame::Up;

  if (count > 0) {
    uint8_t p[8];
    if (readBytes(REG_POINT1, p, sizeof(p))) {
      // p[0] is the track id; coordinates follow as little-endian pairs.
      int16_t rawX = (int16_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
      int16_t rawY = (int16_t)((uint16_t)p[3] | ((uint16_t)p[4] << 8));

      if (TOUCH_SWAP_XY) {
        int16_t swap = rawX;
        rawX = rawY;
        rawY = swap;
      }
      if (TOUCH_INVERT_X) rawX = (int16_t)(SCREEN_W - 1 - rawX);

      *x = rawX;
      *y = rawY;
      result = Frame::Down;
    } else {
      // Could not read the point itself; do not claim a release from that.
      result = Frame::Stale;
    }
  }

  // The ready flag must be cleared or the controller stops reporting.
  writeByte(REG_STATUS, 0);
  return result;
}

}  // namespace

bool begin() {
  g_present = false;

  // Which address the GT911 answers on depends on the INT pin level during its
  // reset, which is not fully under our control here (TP_RST hangs off the
  // CH422G). Probing both is more reliable than trying to force the timing.
  if (probe(0x5D) || probe(0x14)) {
    g_present = true;
    Serial.printf("[touch] GT911 found at 0x%02X\n", g_addr);
    return true;
  }

  g_addr = 0;
  Serial.println("[touch] no GT911 — buttons only");
  return false;
}

bool present() { return g_present; }

uint8_t address() { return g_addr; }

buttons::Event poll() {
  if (!g_present) return buttons::EVENT_NONE;

  uint32_t now = millis();

  // Give up on a contact the controller has stopped talking about, so a dropped
  // release cannot leave the frame believing a finger is still down.
  if (g_down && now - g_downAt > TOUCH_MAX_HOLD_MS) {
    Serial.println("[touch] contact timed out — releasing");
    g_down = false;
    g_longFired = false;
    return buttons::EVENT_NONE;
  }

  if (now - g_lastPoll < TOUCH_POLL_INTERVAL_MS) {
    // Between polls a hold can still mature, so that a long press feels as
    // prompt by finger as it does by button.
    if (g_down && !g_longFired && now - g_downAt >= BTN_LONG_PRESS_MS) {
      g_longFired = true;
      return g_downX < SCREEN_W / 2 ? buttons::LEFT_LONG : buttons::RIGHT_LONG;
    }
    return buttons::EVENT_NONE;
  }
  g_lastPoll = now;

  int16_t x = 0;
  int16_t y = 0;
  Frame frame = readFrame(&x, &y);

  if (frame == Frame::Stale) {
    // Nothing new. Hold maturation is handled above, so there is nothing to do.
    return buttons::EVENT_NONE;
  }

  if (frame == Frame::Down) {
    if (!g_down) {
      g_down = true;
      g_downAt = now;
      g_downX = constrain(x, (int16_t)0, (int16_t)(SCREEN_W - 1));
      g_longFired = false;
      // Logged so a panel reporting rotated or scaled coordinates is obvious
      // from the console rather than as mysteriously reversed buttons.
      Serial.printf("[touch] down at %d,%d (%s half)\n", x, y,
                    g_downX < SCREEN_W / 2 ? "left" : "right");
      return buttons::EVENT_NONE;  // wait for the release to decide tap vs hold
    }
    if (!g_longFired && now - g_downAt >= BTN_LONG_PRESS_MS) {
      g_longFired = true;
      return g_downX < SCREEN_W / 2 ? buttons::LEFT_LONG : buttons::RIGHT_LONG;
    }
    return buttons::EVENT_NONE;
  }

  if (!g_down) return buttons::EVENT_NONE;

  // Lifted. A hold already announced itself.
  g_down = false;
  if (g_longFired) return buttons::EVENT_NONE;
  if (now - g_downAt < TOUCH_MIN_TAP_MS) return buttons::EVENT_NONE;  // brush-off
  return g_downX < SCREEN_W / 2 ? buttons::LEFT_SHORT : buttons::RIGHT_SHORT;
}

bool held() { return g_down; }

}  // namespace touch

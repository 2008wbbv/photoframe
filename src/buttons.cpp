#include "buttons.h"

#include <Arduino.h>

#include "config.h"

namespace buttons {
namespace {

struct Button {
  int8_t pin;
  bool stable;          // debounced level, true = pressed
  bool lastRaw;         // last sampled level
  uint32_t lastChange;  // when lastRaw last flipped
  uint32_t pressedAt;
  bool longFired;       // long press already reported for this hold
  Event shortEvent;
  Event longEvent;
};

Button g_left = {PIN_BTN_LEFT,  false, false, 0, 0, false, LEFT_SHORT,  LEFT_LONG};
Button g_right = {PIN_BTN_RIGHT, false, false, 0, 0, false, RIGHT_SHORT, RIGHT_LONG};

Event update(Button &b, uint32_t now) {
  // Active low: pressed pulls the pin to ground.
  bool raw = (digitalRead(b.pin) == LOW);

  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChange = now;
    return EVENT_NONE;
  }

  if (now - b.lastChange < BTN_DEBOUNCE_MS || raw == b.stable) {
    // Still settling, or nothing new — but a hold can mature mid-press.
    if (b.stable && !b.longFired && now - b.pressedAt >= BTN_LONG_PRESS_MS) {
      b.longFired = true;
      return b.longEvent;
    }
    return EVENT_NONE;
  }

  b.stable = raw;
  if (b.stable) {
    b.pressedAt = now;
    b.longFired = false;
    return EVENT_NONE;
  }

  // Released. A hold already reported itself, so stay quiet.
  if (b.longFired) return EVENT_NONE;
  return b.shortEvent;
}

}  // namespace

void begin() {
  pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);

  // Seed the debounce state from the actual pin levels so a button held down at
  // power-on is not mistaken for a fresh press.
  uint32_t now = millis();
  g_left.lastRaw = g_left.stable = (digitalRead(PIN_BTN_LEFT) == LOW);
  g_right.lastRaw = g_right.stable = (digitalRead(PIN_BTN_RIGHT) == LOW);
  g_left.lastChange = g_right.lastChange = now;
  g_left.pressedAt = g_right.pressedAt = now;
  g_left.longFired = g_left.stable;
  g_right.longFired = g_right.stable;
}

Event poll() {
  uint32_t now = millis();
  Event e = update(g_left, now);
  if (e != EVENT_NONE) return e;
  return update(g_right, now);
}

bool anyHeld() { return g_left.stable || g_right.stable; }

}  // namespace buttons

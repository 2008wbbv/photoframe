#include "buttons.h"

#include <Arduino.h>

#include "config.h"

namespace buttons {
namespace {

enum class Held : uint8_t { None, Left, Right };

// Debounce runs on the decoded button rather than on a raw pin level, so ADC
// noise and the brief slide through intermediate voltages while a switch closes
// both get filtered by the same mechanism.
Held g_stable = Held::None;  // committed state
Held g_seen = Held::None;    // most recent classification
uint32_t g_lastChange = 0;   // when g_seen last differed
uint32_t g_pressedAt = 0;
bool g_longFired = false;
uint32_t g_lastMv = 0;

uint32_t readMillivolts() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < BTN_ADC_SAMPLES; i++) {
    total += analogReadMilliVolts(PIN_BTN_ADC);
  }
  return total / BTN_ADC_SAMPLES;
}

// Voltages between the bands deliberately read as None — a switch that is not
// properly closed should do nothing rather than something surprising.
Held classify(uint32_t mv) {
  if (mv <= BTN_MV_LEFT_MAX) return Held::Left;
  if (mv >= BTN_MV_RIGHT_MIN && mv <= BTN_MV_RIGHT_MAX) return Held::Right;
  return Held::None;  // includes the idle band above BTN_MV_IDLE_MIN
}

Event shortEvent(Held h) {
  return h == Held::Left ? LEFT_SHORT : RIGHT_SHORT;
}

Event longEvent(Held h) { return h == Held::Left ? LEFT_LONG : RIGHT_LONG; }

}  // namespace

void begin() {
  // 12 bits over the full range, so the ladder's three levels land where
  // config.h expects them.
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BTN_ADC, ADC_11db);

  // Seed from the actual voltage, so a button held down at power-on is not
  // mistaken for a fresh press.
  g_lastMv = readMillivolts();
  g_stable = g_seen = classify(g_lastMv);
  g_lastChange = g_pressedAt = millis();
  g_longFired = (g_stable != Held::None);
}

Event poll() {
  uint32_t now = millis();
  g_lastMv = readMillivolts();
  Held current = classify(g_lastMv);

  if (current != g_seen) {
    g_seen = current;
    g_lastChange = now;
    return EVENT_NONE;
  }

  if (now - g_lastChange < BTN_DEBOUNCE_MS || current == g_stable) {
    // Nothing new to commit — but a hold can mature part-way through a press.
    if (g_stable != Held::None && !g_longFired &&
        now - g_pressedAt >= BTN_LONG_PRESS_MS) {
      g_longFired = true;
      return longEvent(g_stable);
    }
    return EVENT_NONE;
  }

  Held previous = g_stable;
  g_stable = current;

  if (current != Held::None) {
    // A new press. Rolling straight from one button to the other without
    // passing through idle starts the new press and drops the old one, which is
    // rare enough not to be worth the extra event queue.
    g_pressedAt = now;
    g_longFired = false;
    return EVENT_NONE;
  }

  // Released. A hold already announced itself, so stay quiet.
  if (g_longFired || previous == Held::None) return EVENT_NONE;
  return shortEvent(previous);
}

bool anyHeld() { return g_stable != Held::None; }

uint32_t lastMillivolts() { return g_lastMv; }

}  // namespace buttons

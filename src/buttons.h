// Two momentary buttons sharing a single ADC pin.
//
// The board brings out only one usable GPIO on its connectors, so the buttons
// are wired as a resistor ladder on GPIO6 and told apart by voltage rather than
// by having a pin each. See config.h for the ladder and docs/WIRING.md for how
// to build it.
//
// Each button reports a short press on release and a long press the moment it
// crosses the hold threshold — so holding gives immediate feedback rather than
// waiting for you to let go, and a hold never also fires a short press.

#pragma once

#include <stdint.h>

namespace buttons {

enum Event : uint8_t {
  EVENT_NONE = 0,
  LEFT_SHORT,
  LEFT_LONG,
  RIGHT_SHORT,
  RIGHT_LONG,
};

void begin();

// Call every loop. Returns at most one event per call.
Event poll();

// True while either button is physically down. Used to hold a menu open.
bool anyHeld();

// Last averaged reading in millivolts. Handy when checking a fresh solder job
// against the bands in config.h.
uint32_t lastMillivolts();

}  // namespace buttons

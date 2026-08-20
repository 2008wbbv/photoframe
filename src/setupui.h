// First-boot wizard: hardware check, then calibrating the dimming.
//
// Runs before the slideshow the first time the frame starts, and can be
// re-entered later from the web page. Two parts:
//
//   1. Every subsystem listed with a live reading beside it, so a wiring mistake
//      is visible while the soldering iron is still warm rather than after the
//      thing is glued into a frame.
//
//   2. Turn the room lights on, record. Turn them off, record. Those two numbers
//      become the ends of the brightness curve, which beats the generic indoor
//      figures the firmware would otherwise guess at — a bright kitchen and a dim
//      bedroom are not the same room.
//
// Driven by the same events as everything else, so buttons and touch both work.

#pragma once

#include <stdint.h>

#include "buttons.h"

namespace setupui {

// Starts the wizard from the top.
void begin();

// True while it owns the screen.
bool active();

// Feed it button/touch events. Returns true once the wizard has finished and the
// caller should take the screen back.
bool handle(buttons::Event e);

// Call every loop while active, to keep the live lux reading moving.
void tick();

}  // namespace setupui

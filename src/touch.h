// GT911 capacitive touch, as a backup to the physical buttons.
//
// Deliberately emits the same events the buttons do, so the state machine does
// not need to know where an action came from. The screen is split down the
// middle: the left half behaves like the left button, the right half like the
// right button, and a hold is a hold.
//
//   tap left    previous photo
//   tap right   next photo
//   hold right  QR code
//   hold left   interval picker
//
// That means every control is reachable by touch if a button ever fails, and
// menus dismiss the same way either way.

#pragma once

#include <stdint.h>

#include "buttons.h"

namespace touch {

// Probes the GT911 at both of its possible addresses. Returns false if neither
// answers, in which case poll() is inert and the buttons still work.
bool begin();

bool present();

// The address the controller was found on, for the boot log.
uint8_t address();

// Call every loop. Returns at most one event per call, using the same
// vocabulary as buttons::poll().
buttons::Event poll();

// True while a finger is down, so a menu stays open under a long press exactly
// as it does for a held button.
bool held();

}  // namespace touch

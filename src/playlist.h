// The randomised playlist.
//
// Order is a Fisher-Yates shuffle of the photo list, held for a full pass so
// that "back" really returns to the photo you just saw rather than jumping
// somewhere new. When a pass completes the order is reshuffled, taking care not
// to start the new pass with the photo that just ended the old one.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace playlist {

// Rescans /photos and builds a fresh shuffled order.
bool rescan();

uint16_t count();
bool empty();

// Filename of the photo the playlist is currently pointing at.
String current();

// Advance / go back through the shuffled order. Both wrap around; advance()
// reshuffles when it wraps.
void advance();
void back();

// Where we are in the current pass, for the web UI.
uint16_t position();

// Filename of whatever advance() would land on, without moving. Used to decode
// one photo ahead so that advancing is instant.
String peekNext();

}  // namespace playlist

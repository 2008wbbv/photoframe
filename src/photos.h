// JPEG decoding from the SD card into an RGB565 buffer.
//
// Photos uploaded through the web UI are already exactly 800x480 — the browser
// crops and resizes them before sending, which also means the frame accepts
// whatever the phone can open, HEIC included, without needing to decode those
// formats itself. Files you drop onto the card by hand can be any size; those
// get downscaled by the nearest power of two the decoder supports and centred.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace photos {

// Decodes /photos/<name> into `dest`, which must hold a full 800x480 frame.
// Areas not covered by the image are filled black.
bool decodeInto(const String &name, uint16_t *dest);

}  // namespace photos

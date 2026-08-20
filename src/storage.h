// SD card access and persisted settings.
//
// The card's chip-select is not a GPIO — it is CH422G EXIO4 — so ch422g::begin()
// must have run before storage::begin(). We hold that CS asserted permanently
// and give the Arduino SD library a spare pin to toggle instead; the card is the
// only device on the bus, so nothing else is listening.

#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "config.h"

namespace storage {

struct Settings {
  uint8_t intervalIndex = INTERVAL_DEFAULT_INDEX;
  bool autoDim = true;
  uint8_t brightnessMin = BRIGHTNESS_MIN_DEFAULT;
  uint8_t brightnessMax = BRIGHTNESS_MAX_DEFAULT;
  float nightLux = NIGHT_LUX_DEFAULT;

  // Measured in the room the frame actually lives in, rather than guessed.
  // luxBright is what the room reads with the lights on, luxDark with them off;
  // the brightness curve runs between those two points.
  bool calibrated = false;
  float luxBright = LUX_AT_MAX_BRIGHTNESS;
  float luxDark = LUX_AT_MIN_BRIGHTNESS;
};

// Mounts the card and makes sure /photos and /thumbs exist.
bool begin();
bool mounted();

uint64_t cardSizeBytes();
uint64_t cardUsedBytes();

// Settings are stored as plain key=value lines in /config.txt so you can edit
// them with a text editor if you ever pull the card.
Settings &settings();
bool loadSettings();
bool saveSettings();

// Strips anything that could escape the photos directory and forces a .jpg
// extension. Returns an empty string if nothing usable is left.
String sanitizeName(const String &raw);

// Filenames in /photos, in directory order. Caps out at MAX_PHOTOS.
bool listPhotos(String *out, uint16_t maxCount, uint16_t *countOut);

// Removes a photo and its thumbnail.
bool deletePhoto(const String &name);

bool photoExists(const String &name);

}  // namespace storage

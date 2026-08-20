#include "storage.h"

#include <SD.h>
#include <SPI.h>

#include "ch422g.h"

namespace storage {
namespace {

SPIClass g_spi(FSPI);
bool g_mounted = false;
Settings g_settings;

bool isSafeNameChar(char c) {
  return isalnum((int)c) || c == '_' || c == '-';
}

String trimmed(const String &s) {
  String out = s;
  out.trim();
  return out;
}

}  // namespace

bool begin() {
  // Assert the real chip-select and leave it asserted for good.
  ch422g::write(ch422g::EXIO_SD_CS, false);

  g_spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_DUMMY_CS);

  if (!SD.begin(PIN_SD_DUMMY_CS, g_spi, SD_SPI_HZ)) {
    Serial.println("[sd] mount failed — card inserted and FAT32 formatted?");
    g_mounted = false;
    return false;
  }

  if (SD.cardType() == CARD_NONE) {
    Serial.println("[sd] no card detected");
    g_mounted = false;
    return false;
  }

  g_mounted = true;
  if (!SD.exists(DIR_PHOTOS)) SD.mkdir(DIR_PHOTOS);
  if (!SD.exists(DIR_THUMBS)) SD.mkdir(DIR_THUMBS);

  Serial.printf("[sd] mounted, %llu MB total\n", cardSizeBytes() / (1024 * 1024));
  return true;
}

bool mounted() { return g_mounted; }

uint64_t cardSizeBytes() { return g_mounted ? SD.cardSize() : 0; }

uint64_t cardUsedBytes() { return g_mounted ? SD.usedBytes() : 0; }

Settings &settings() { return g_settings; }

bool loadSettings() {
  if (!g_mounted || !SD.exists(PATH_CONFIG)) return false;

  File f = SD.open(PATH_CONFIG, FILE_READ);
  if (!f) return false;

  while (f.available()) {
    String line = trimmed(f.readStringUntil('\n'));
    if (line.length() == 0 || line.startsWith("#")) continue;

    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = trimmed(line.substring(0, eq));
    String value = trimmed(line.substring(eq + 1));

    if (key == "interval_index") {
      uint8_t v = (uint8_t)value.toInt();
      if (v < INTERVAL_OPTION_COUNT) g_settings.intervalIndex = v;
    } else if (key == "auto_dim") {
      g_settings.autoDim = (value.toInt() != 0);
    } else if (key == "brightness_min") {
      g_settings.brightnessMin = (uint8_t)constrain(value.toInt(), 0, 255);
    } else if (key == "brightness_max") {
      g_settings.brightnessMax = (uint8_t)constrain(value.toInt(), 0, 255);
    } else if (key == "night_lux") {
      g_settings.nightLux = value.toFloat();
    } else if (key == "calibrated") {
      g_settings.calibrated = (value.toInt() != 0);
    } else if (key == "lux_bright") {
      g_settings.luxBright = value.toFloat();
    } else if (key == "lux_dark") {
      g_settings.luxDark = value.toFloat();
    }
  }
  f.close();

  if (g_settings.brightnessMax < g_settings.brightnessMin) {
    g_settings.brightnessMax = g_settings.brightnessMin;
  }
  // A curve whose endpoints are equal or inverted would divide by zero in the
  // logarithm, so refuse to trust a calibration that does not make sense.
  if (g_settings.luxDark < 0.05f) g_settings.luxDark = 0.05f;
  if (g_settings.luxBright <= g_settings.luxDark * 2.0f) {
    g_settings.calibrated = false;
    g_settings.luxDark = LUX_AT_MIN_BRIGHTNESS;
    g_settings.luxBright = LUX_AT_MAX_BRIGHTNESS;
  }
  return true;
}

bool saveSettings() {
  if (!g_mounted) return false;

  File f = SD.open(PATH_CONFIG, FILE_WRITE);
  if (!f) return false;

  f.println("# Rachel's Frame settings");
  f.printf("interval_index=%u\n", g_settings.intervalIndex);
  f.printf("auto_dim=%d\n", g_settings.autoDim ? 1 : 0);
  f.printf("brightness_min=%u\n", g_settings.brightnessMin);
  f.printf("brightness_max=%u\n", g_settings.brightnessMax);
  f.printf("night_lux=%.2f\n", g_settings.nightLux);
  f.printf("calibrated=%d\n", g_settings.calibrated ? 1 : 0);
  f.printf("lux_bright=%.2f\n", g_settings.luxBright);
  f.printf("lux_dark=%.2f\n", g_settings.luxDark);
  f.close();
  return true;
}

String sanitizeName(const String &raw) {
  // Take only the basename — anything before a separator is discarded outright
  // rather than rewritten, so no ../ traversal can survive.
  int slash = max((int)raw.lastIndexOf('/'), (int)raw.lastIndexOf('\\'));
  String base = (slash >= 0) ? raw.substring(slash + 1) : raw;

  int dot = base.lastIndexOf('.');
  if (dot > 0) base = base.substring(0, dot);

  String clean;
  for (size_t i = 0; i < base.length() && clean.length() < 24; i++) {
    char c = base[i];
    if (isSafeNameChar(c)) clean += c;
  }
  if (clean.length() == 0) return String();
  return clean + ".jpg";
}

bool listPhotos(String *out, uint16_t maxCount, uint16_t *countOut) {
  *countOut = 0;
  if (!g_mounted) return false;

  File dir = SD.open(DIR_PHOTOS);
  if (!dir || !dir.isDirectory()) return false;

  uint16_t n = 0;
  File entry;
  while (n < maxCount && (entry = dir.openNextFile())) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      // Some core versions hand back a full path here, others just the leaf.
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);

      String lower = name;
      lower.toLowerCase();
      if (!name.startsWith(".") &&
          (lower.endsWith(".jpg") || lower.endsWith(".jpeg"))) {
        out[n++] = name;
      }
    }
    entry.close();
  }
  dir.close();

  *countOut = n;
  return true;
}

bool deletePhoto(const String &name) {
  if (!g_mounted) return false;
  String safe = sanitizeName(name);
  if (safe.length() == 0) return false;

  bool ok = SD.remove(String(DIR_PHOTOS) + "/" + safe);
  SD.remove(String(DIR_THUMBS) + "/" + safe);  // thumbnail is best-effort
  return ok;
}

bool photoExists(const String &name) {
  if (!g_mounted) return false;
  String safe = sanitizeName(name);
  if (safe.length() == 0) return false;
  return SD.exists(String(DIR_PHOTOS) + "/" + safe);
}

}  // namespace storage

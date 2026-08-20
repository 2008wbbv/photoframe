#include "setupui.h"

#include <Arduino.h>

#include "bh1750.h"
#include "config.h"
#include "display.h"
#include "playlist.h"
#include "storage.h"
#include "touch.h"
#include "wifimgr.h"

namespace setupui {
namespace {

enum class Step : uint8_t {
  Idle,
  Hardware,   // what is detected, with live readings
  Bright,     // lights on
  Dark,       // lights off
  TooClose,   // the two readings were not different enough to be useful
  Done,
};

Step g_step = Step::Idle;
float g_bright = 0.0f;
float g_dark = 0.0f;
uint32_t g_lastDraw = 0;

// Buffers for the diagnostic values, which have to outlive the draw call since
// DiagRow holds pointers rather than copies.
char g_vLight[28];
char g_vTouch[28];
char g_vCard[28];
char g_vButtons[28];
char g_vWifi[40];
char g_vPhotos[28];

float lux() { return bh1750::present() ? bh1750::readLux() : -1.0f; }

void drawHardware() {
  float l = lux();

  if (bh1750::present()) {
    snprintf(g_vLight, sizeof(g_vLight), "0x%02X  %.0f lux", bh1750::address(),
             (double)(l < 0 ? 0 : l));
  } else {
    snprintf(g_vLight, sizeof(g_vLight), "not found");
  }

  if (touch::present()) {
    snprintf(g_vTouch, sizeof(g_vTouch), "0x%02X", touch::address());
  } else {
    snprintf(g_vTouch, sizeof(g_vTouch), "not found");
  }

  if (storage::mounted()) {
    snprintf(g_vCard, sizeof(g_vCard), "%llu GB",
             (unsigned long long)(storage::cardSizeBytes() / (1024ULL * 1024 * 1024)));
  } else {
    snprintf(g_vCard, sizeof(g_vCard), "no card");
  }

  snprintf(g_vPhotos, sizeof(g_vPhotos), "%u", playlist::count());
  snprintf(g_vButtons, sizeof(g_vButtons), "%u mV",
           (unsigned)buttons::lastMillivolts());

  if (wifimgr::mode() == wifimgr::Mode::Station) {
    snprintf(g_vWifi, sizeof(g_vWifi), "%s", wifimgr::ssid().c_str());
  } else {
    snprintf(g_vWifi, sizeof(g_vWifi), "setup portal");
  }

  // The light sensor answering on 0x23 is a pass in the sense that it replied,
  // but it is sharing an address with the I/O expander and will cause trouble,
  // so it is flagged as a failure here where it can still be fixed easily.
  bool lightOk = bh1750::present() && bh1750::address() == BH1750_ADDR_HIGH;
  bool buttonsOk = buttons::lastMillivolts() >= BTN_MV_IDLE_MIN;

  const display::DiagRow rows[] = {
      {"Light sensor", g_vLight, lightOk},
      {"Touchscreen", g_vTouch, touch::present()},
      {"Buttons at rest", g_vButtons, buttonsOk},
      {"SD card", g_vCard, storage::mounted()},
      {"Photos found", g_vPhotos, true},
      {"Wi-Fi", g_vWifi, true},
  };

  const char *footer = bh1750::present()
                           ? "right to calibrate the dimming"
                           : "right to skip - no light sensor";
  display::drawDiagnostics(rows, sizeof(rows) / sizeof(rows[0]), footer);
}

void drawStep() {
  switch (g_step) {
    case Step::Hardware:
      drawHardware();
      break;

    case Step::Bright:
      display::drawCalibration("STEP 1 OF 2", "Lights on",
                               "Turn the room lights on, the way",
                               "it normally is during the day.", lux(),
                               "right to record  -  left to go back");
      break;

    case Step::Dark:
      display::drawCalibration("STEP 2 OF 2", "Lights off",
                               "Now turn them off, as dark as",
                               "the room actually gets at night.", lux(),
                               "right to record  -  left to go back");
      break;

    case Step::TooClose:
      display::drawCalibration(
          "THAT DID NOT WORK", "Too similar",
          "Both readings were nearly the same, so there is",
          "nothing to dim between. Check the sensor faces the room.", -1.0f,
          "right to try again");
      break;

    case Step::Done: {
      static char summary[64];
      snprintf(summary, sizeof(summary), "%.0f lux bright  /  %.1f lux dark",
               (double)g_bright, (double)g_dark);
      display::drawCalibration("ALL SET", "Ready", summary,
                               "The frame will dim between those two.", -1.0f,
                               "right to start the slideshow");
      break;
    }

    case Step::Idle:
      break;
  }
  g_lastDraw = millis();
}

// Averages a few readings so a flicker or a passing shadow does not become the
// calibration point.
float sampleLux() {
  float total = 0.0f;
  uint8_t taken = 0;
  for (uint8_t i = 0; i < CALIB_SAMPLES; i++) {
    float l = lux();
    if (l >= 0.0f) {
      total += l;
      taken++;
    }
    delay(CALIB_SAMPLE_GAP_MS);
  }
  return taken ? total / taken : -1.0f;
}

void finish() {
  storage::Settings &s = storage::settings();
  s.luxBright = g_bright;
  s.luxDark = g_dark < 0.05f ? 0.05f : g_dark;
  s.calibrated = true;

  // Below the calibrated dark point the room is darker than she ever leaves it,
  // which is the moment to blank rather than merely dim.
  s.nightLux = s.luxDark * 0.6f;
  if (s.nightLux < 0.05f) s.nightLux = 0.05f;

  storage::saveSettings();
  Serial.printf("[setup] calibrated: bright %.1f lux, dark %.2f lux, night %.2f\n",
                (double)s.luxBright, (double)s.luxDark, (double)s.nightLux);
}

}  // namespace

void begin() {
  g_step = Step::Hardware;
  g_bright = 0.0f;
  g_dark = 0.0f;
  drawStep();
}

bool active() { return g_step != Step::Idle; }

void tick() {
  if (g_step == Step::Idle) return;
  // Only the screens showing a live reading need repainting.
  if (g_step != Step::Hardware && g_step != Step::Bright && g_step != Step::Dark) {
    return;
  }
  if (millis() - g_lastDraw >= CALIB_REFRESH_MS) drawStep();
}

bool handle(buttons::Event e) {
  if (g_step == Step::Idle || e == buttons::EVENT_NONE) return false;

  bool forward = (e == buttons::RIGHT_SHORT || e == buttons::RIGHT_LONG);
  bool back = (e == buttons::LEFT_SHORT || e == buttons::LEFT_LONG);

  switch (g_step) {
    case Step::Hardware:
      if (forward) {
        // No sensor means nothing to calibrate, so do not pretend otherwise.
        g_step = bh1750::present() ? Step::Bright : Step::Idle;
        if (g_step == Step::Idle) return true;
        drawStep();
      }
      break;

    case Step::Bright:
      if (forward) {
        g_bright = sampleLux();
        g_step = Step::Dark;
        drawStep();
      } else if (back) {
        g_step = Step::Hardware;
        drawStep();
      }
      break;

    case Step::Dark:
      if (forward) {
        g_dark = sampleLux();
        // Needs a real gap between the two, or the curve has no range to work
        // across and every room would look the same brightness.
        if (g_bright <= 0.0f || g_dark < 0.0f ||
            g_bright < g_dark * CALIB_MIN_RATIO) {
          g_step = Step::TooClose;
        } else {
          finish();
          g_step = Step::Done;
        }
        drawStep();
      } else if (back) {
        g_step = Step::Bright;
        drawStep();
      }
      break;

    case Step::TooClose:
      if (forward || back) {
        g_step = Step::Bright;
        drawStep();
      }
      break;

    case Step::Done:
      if (forward || back) {
        g_step = Step::Idle;
        return true;
      }
      break;

    case Step::Idle:
      break;
  }
  return false;
}

}  // namespace setupui

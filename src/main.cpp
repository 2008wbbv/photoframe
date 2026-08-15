// Rachel's Frame — a digital picture frame for the Waveshare
// ESP32-S3-Touch-LCD-4.3.
//
//   left  tap   previous photo
//   right tap   next photo
//   right hold  QR code to join the frame's Wi-Fi and upload more
//   left  hold  how long each photo stays up
//
// A BH1750 on the shared I2C bus dims the picture as the room darkens, and
// blanks the panel entirely at night until a button wakes it.

#include <Arduino.h>
#include <Wire.h>

#include "bh1750.h"
#include "buttons.h"
#include "ch422g.h"
#include "config.h"
#include "display.h"
#include "photos.h"
#include "playlist.h"
#include "storage.h"
#include "webui.h"

namespace {

enum class State : uint8_t { Slideshow, QrOverlay, IntervalMenu };

State g_state = State::Slideshow;
uint32_t g_stateEnteredAt = 0;
uint32_t g_lastInteraction = 0;

// Slideshow
uint32_t g_shownAt = 0;
String g_preparedName;  // photo already sitting in the back buffer, if any

// When there is no photo to show we put a card up instead. It has to be
// remembered rather than just drawn, because anything that re-blits the
// framebuffer — a brightness step, dismissing an overlay — wipes it.
enum class Message : uint8_t { None, NoPhotos, Unreadable };
Message g_message = Message::None;

// Dimming
float g_lux = -1.0f;
float g_luxSmoothed = -1.0f;
uint32_t g_lastLuxSample = 0;
uint32_t g_lastSlew = 0;
uint8_t g_targetBrightness = BRIGHTNESS_MAX_DEFAULT;
bool g_nightMode = false;
uint32_t g_wakeUntil = 0;

uint32_t intervalMs() {
  return INTERVAL_OPTIONS_MS[storage::settings().intervalIndex];
}

const char *intervalLabel(uint8_t index) {
  static const char *const labels[] = {"10 sec", "30 sec", "1 min",  "5 min",
                                       "15 min", "30 min", "1 hour"};
  return index < INTERVAL_OPTION_COUNT ? labels[index] : "?";
}

float currentLux() { return g_lux; }

// ---------------------------------------------------------------------------
// Overlays
// ---------------------------------------------------------------------------

// Redraws whatever should currently sit on top of the framebuffer. Called after
// anything that re-blits, so cards survive brightness changes and dismissals.
void drawOverlay() {
  switch (g_state) {
    case State::QrOverlay:
      display::drawWifiQr(AP_SSID, AP_PASSWORD, webui::url());
      return;
    case State::IntervalMenu: {
      uint8_t i = storage::settings().intervalIndex;
      display::drawIntervalMenu(intervalLabel(i), i, INTERVAL_OPTION_COUNT);
      return;
    }
    case State::Slideshow:
      break;
  }

  switch (g_message) {
    case Message::NoPhotos:
      // Nothing to show yet, so lead with the way to fix that.
      display::drawWifiQr(AP_SSID, AP_PASSWORD, webui::url());
      break;
    case Message::Unreadable:
      display::drawMessage("Can't read those photos", "Try re-uploading them",
                           webui::url());
      break;
    case Message::None:
      break;
  }
}

bool hasOverlay() {
  return g_state != State::Slideshow || g_message != Message::None;
}

void enterState(State next) {
  g_state = next;
  g_stateEnteredAt = millis();
  // Drop back to the underlying photo, wiping whatever card was on top of it,
  // then redraw whatever the new state calls for.
  display::repaint();
  drawOverlay();
}

// ---------------------------------------------------------------------------
// Slideshow
// ---------------------------------------------------------------------------

void showMessage(Message which) {
  g_message = which;
  display::fillFront(0x0000);
  drawOverlay();
}

// Renders whatever the playlist is pointing at. Uses the pre-decoded back
// buffer when it happens to hold the right photo, which is the common case for
// tapping right or letting the timer run.
void showCurrent() {
  if (playlist::empty()) {
    showMessage(Message::NoPhotos);
    g_shownAt = millis();
    return;
  }

  // A corrupt file should not wedge the frame — step past it and try the next.
  // Bounded by the playlist length so a card full of junk still terminates.
  // Avoid Arduino's min() here — it is a macro in some core versions, so the
  // explicit form is the portable one.
  uint16_t attempts = playlist::count() < 5 ? playlist::count() : 5;
  for (uint16_t i = 0; i < attempts; i++) {
    String name = playlist::current();
    if (name.length() == 0) break;

    bool ready = (name == g_preparedName) ||
                 photos::decodeInto(name, display::backBuffer());
    if (ready) {
      display::swapBuffers();
      g_preparedName = "";
      g_message = Message::None;
      g_shownAt = millis();
      return;
    }

    Serial.printf("[frame] skipping unreadable %s\n", name.c_str());
    playlist::advance();
  }

  showMessage(Message::Unreadable);
  g_shownAt = millis();
}

// Decodes the next photo into the back buffer while the current one is up, so
// advancing is instant. Costs a few hundred milliseconds right after a
// transition, when nobody is pressing anything.
void prepareNext() {
  g_preparedName = "";
  if (playlist::count() < 2) return;

  String next = playlist::peekNext();
  if (next.length() == 0) return;  // about to reshuffle, so it is not knowable

  if (photos::decodeInto(next, display::backBuffer())) {
    g_preparedName = next;
  }
}

void goNext() {
  playlist::advance();
  showCurrent();
  prepareNext();
}

void goPrev() {
  playlist::back();
  showCurrent();
  prepareNext();
}

void onPhotosChanged() {
  playlist::rescan();
  g_preparedName = "";
  // If the frame had nothing to show, start showing something immediately.
  if (g_message != Message::None && !playlist::empty() &&
      g_state == State::Slideshow) {
    showCurrent();
    prepareNext();
  }
}

// ---------------------------------------------------------------------------
// Auto-dimming
// ---------------------------------------------------------------------------

uint8_t brightnessForLux(float lux) {
  const storage::Settings &s = storage::settings();
  if (lux <= LUX_AT_MIN_BRIGHTNESS) return s.brightnessMin;
  if (lux >= LUX_AT_MAX_BRIGHTNESS) return s.brightnessMax;

  // Logarithmic, because perceived brightness is.
  float t = logf(lux / LUX_AT_MIN_BRIGHTNESS) /
            logf(LUX_AT_MAX_BRIGHTNESS / LUX_AT_MIN_BRIGHTNESS);
  return (uint8_t)(s.brightnessMin + t * (s.brightnessMax - s.brightnessMin));
}

void updateDimming() {
  uint32_t now = millis();
  const storage::Settings &s = storage::settings();

  if (bh1750::present() && now - g_lastLuxSample >= LUX_SAMPLE_INTERVAL_MS) {
    g_lastLuxSample = now;
    float lux = bh1750::readLux();
    if (lux >= 0.0f) {
      g_lux = lux;
      g_luxSmoothed = (g_luxSmoothed < 0.0f)
                          ? lux
                          : g_luxSmoothed + LUX_SMOOTHING * (lux - g_luxSmoothed);
    }
  }

  if (!s.autoDim || !bh1750::present()) {
    g_targetBrightness = s.brightnessMax;
    g_nightMode = false;
    display::setPanelOn(true);
  } else if (g_luxSmoothed >= 0.0f) {
    // A little hysteresis on the way out of night mode, so a candle flicker
    // does not strobe the panel.
    if (g_nightMode) {
      if (g_luxSmoothed > s.nightLux * 2.0f) g_nightMode = false;
    } else {
      if (g_luxSmoothed < s.nightLux) g_nightMode = true;
    }

    bool awake = !g_nightMode || (int32_t)(g_wakeUntil - now) > 0;
    display::setPanelOn(awake);
    g_targetBrightness =
        g_nightMode ? s.brightnessMin : brightnessForLux(g_luxSmoothed);
  }

  // Ease toward the target rather than snapping to it.
  if (now - g_lastSlew >= BRIGHTNESS_SLEW_INTERVAL_MS) {
    g_lastSlew = now;
    uint8_t current = display::brightness();
    if (current != g_targetBrightness) {
      int16_t delta = (int16_t)g_targetBrightness - (int16_t)current;
      int16_t step = constrain(delta, -(int16_t)BRIGHTNESS_SLEW_STEP,
                               (int16_t)BRIGHTNESS_SLEW_STEP);
      display::setBrightness((uint8_t)(current + step));
      // setBrightness re-blits the photo, which wipes any card on top of it.
      if (hasOverlay()) drawOverlay();
    }
  }
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

// Returns true if the press was consumed purely to wake the panel.
bool wakeIfAsleep() {
  if (!g_nightMode) return false;
  bool wasAsleep = !display::panelOn();
  g_wakeUntil = millis() + NIGHT_WAKE_MS;
  display::setPanelOn(true);
  return wasAsleep;
}

void handleButton(buttons::Event e) {
  if (e == buttons::EVENT_NONE) return;

  g_lastInteraction = millis();
  if (wakeIfAsleep()) return;
  if (g_nightMode) g_wakeUntil = millis() + NIGHT_WAKE_MS;

  switch (g_state) {
    case State::Slideshow:
      switch (e) {
        case buttons::LEFT_SHORT:  goPrev(); break;
        case buttons::RIGHT_SHORT: goNext(); break;
        case buttons::RIGHT_LONG:  enterState(State::QrOverlay); break;
        case buttons::LEFT_LONG:   enterState(State::IntervalMenu); break;
        default: break;
      }
      break;

    case State::QrOverlay:
      // Any press dismisses it.
      enterState(State::Slideshow);
      break;

    case State::IntervalMenu: {
      storage::Settings &s = storage::settings();
      if (e == buttons::RIGHT_SHORT) {
        s.intervalIndex = (uint8_t)((s.intervalIndex + 1) % INTERVAL_OPTION_COUNT);
        drawOverlay();
      } else if (e == buttons::LEFT_SHORT) {
        s.intervalIndex = (uint8_t)((s.intervalIndex + INTERVAL_OPTION_COUNT - 1) %
                                    INTERVAL_OPTION_COUNT);
        drawOverlay();
      } else {
        // Either hold closes the menu.
        storage::saveSettings();
        g_shownAt = millis();  // the new interval starts from now
        enterState(State::Slideshow);
      }
      break;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Rachel's Frame ===");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);

  // The expander owns LCD reset, display enable and the SD chip-select, so it
  // has to come up before anything else.
  if (!ch422g::begin()) {
    Serial.println("[boot] CH422G did not respond — check the I2C bus");
  }
  ch422g::resetPanel();

  if (!display::begin()) {
    Serial.println("[boot] display init failed, halting");
    while (true) delay(1000);
  }
  display::fillFront(0x0000);
  display::drawMessage("Rachel's Frame", "starting up", "");

  buttons::begin();
  bh1750::begin();

  if (storage::begin()) {
    storage::loadSettings();
  } else {
    display::fillFront(0x0000);
    display::drawMessage("No SD card", "Insert a FAT32 card", "and restart");
    // Keep going: the web UI still comes up, so the card can be sorted out and
    // the frame restarted without a laptop.
  }

  playlist::rescan();

  webui::Hooks hooks;
  hooks.onNext = goNext;
  hooks.onPrev = goPrev;
  hooks.onPhotosChanged = onPhotosChanged;
  hooks.getLux = currentLux;
  webui::begin(hooks);

  Serial.printf("[boot] ready — %u photos, %s per photo\n", playlist::count(),
                intervalLabel(storage::settings().intervalIndex));

  showCurrent();
  prepareNext();
  g_lastInteraction = millis();
}

void loop() {
  webui::loop();
  handleButton(buttons::poll());
  updateDimming();

  uint32_t now = millis();

  switch (g_state) {
    case State::Slideshow:
      // Advance on the timer, but not while the panel is asleep — no point
      // burning through the shuffle in the dark.
      if (!playlist::empty() && display::panelOn() &&
          now - g_shownAt >= intervalMs()) {
        goNext();
      }
      break;

    case State::QrOverlay:
      if (now - g_stateEnteredAt >= QR_OVERLAY_TIMEOUT_MS) {
        enterState(State::Slideshow);
      }
      break;

    case State::IntervalMenu:
      if (!buttons::anyHeld() &&
          now - g_lastInteraction >= MENU_IDLE_TIMEOUT_MS) {
        storage::saveSettings();
        g_shownAt = now;
        enterState(State::Slideshow);
      }
      break;
  }
}

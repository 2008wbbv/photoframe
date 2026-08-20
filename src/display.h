// Panel bring-up, software dimming, and the on-screen overlays.
//
// Two full-size RGB565 buffers live in PSRAM:
//
//   front  — the image currently on screen, at full brightness
//   back   — where the next photo is decoded, so advancing is instant
//
// Nothing is ever drawn to the panel at full brightness directly. Every photo
// reaches the framebuffer through blit(), which scales pixel values by the
// current brightness. That is how dimming works here at all: the backlight is a
// digital on/off line behind the I2C expander, so there is no PWM to turn down.

#pragma once

#include <stdint.h>

namespace display {

// Brings up the RGB panel and allocates the PSRAM buffers.
// Returns false if the panel or either buffer could not be allocated.
bool begin();

// The buffer a photo decoder should write into.
uint16_t *backBuffer();

// Promotes the back buffer to front and pushes it to the panel. Call this once
// a decode into backBuffer() has completed.
void swapBuffers();

// Re-pushes the front buffer to the panel, applying current brightness. Also
// erases any overlay drawn on top.
void repaint();

// Fills the front buffer with a solid colour and pushes it.
void fillFront(uint16_t color);

// 0..255. Values are applied when blitting, not to the backlight. Setting this
// repaints the screen.
void setBrightness(uint8_t brightness);
uint8_t brightness();

// Blanks the panel via the expander's display-enable line. This is the only
// true "off" available, and it is what night mode uses.
void setPanelOn(bool on);
bool panelOn();

// ---------------------------------------------------------------------------
// Overlays — drawn on top of whatever is on screen. repaint() clears them.
// ---------------------------------------------------------------------------

// A card with a QR code on the left and details beside it.
//
// Two variants, because what the QR should carry depends on which network the
// frame is on. When it hosts its own access point the code is a Wi-Fi join
// payload, so scanning it connects the phone. When it is already on her network
// the phone is too, so the code is just the URL and scanning it opens the page
// directly — no network switching at all.
void drawWifiQr(const char *ssid, const char *password, const char *url);
void drawUrlQr(const char *url, const char *network, const char *alsoAt);

// The interval picker. `label` is the human-readable duration, and the dots
// show which of `count` options is selected.
void drawIntervalMenu(const char *label, uint8_t index, uint8_t count);

// A centred message card, used for "no photos yet" and error states.
void drawMessage(const char *title, const char *line1, const char *line2);

// One line of the first-boot hardware check.
struct DiagRow {
  const char *label;
  const char *value;
  bool ok;
};

// Full-screen hardware check: every subsystem with a live reading beside it.
void drawDiagnostics(const DiagRow *rows, uint8_t count, const char *footer);

// Full-screen calibration step. `lux` is shown live so you can watch it move as
// the room lights change; pass a negative value to hide it.
void drawCalibration(const char *step, const char *title, const char *line1,
                     const char *line2, float lux, const char *footer);

// A note sent to the frame. Wraps to the card width and is not dimmed, so it
// stays readable in a dark room.
void drawNote(const char *from, const char *text);

}  // namespace display

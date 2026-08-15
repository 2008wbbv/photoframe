#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <qrcode.h>

#include "ch422g.h"
#include "config.h"

namespace display {
namespace {

Arduino_ESP32RGBPanel *g_panel = nullptr;
Arduino_RGB_Display *g_gfx = nullptr;

uint16_t *g_front = nullptr;
uint16_t *g_back = nullptr;
uint16_t *g_fb = nullptr;  // the panel's own framebuffer, scanned out by DMA

uint8_t g_brightness = BRIGHTNESS_MAX_DEFAULT;
bool g_panelOn = true;

// Dimming lookup tables, rebuilt whenever brightness changes. Kept in internal
// RAM — they are hit three times per pixel, so PSRAM latency would hurt.
uint8_t g_lut5[32];
uint8_t g_lut6[64];

void rebuildLuts() {
  for (uint8_t i = 0; i < 32; i++) {
    g_lut5[i] = (uint8_t)(((uint16_t)i * g_brightness) / 255);
  }
  for (uint8_t i = 0; i < 64; i++) {
    g_lut6[i] = (uint8_t)(((uint16_t)i * g_brightness) / 255);
  }
}

// Copies src into the panel framebuffer, scaling every channel by brightness.
void blit(const uint16_t *src) {
  if (src == nullptr || g_fb == nullptr) return;

  const uint16_t *in = src;
  uint16_t *out = g_fb;
  for (uint32_t i = 0; i < SCREEN_PIXELS; i++) {
    uint16_t p = *in++;
    *out++ = (uint16_t)((g_lut5[p >> 11] << 11) |
                        (g_lut6[(p >> 5) & 0x3F] << 5) |
                        g_lut5[p & 0x1F]);
  }
}

// Overlay cards are drawn straight into the framebuffer, on top of the already
// dimmed photo. They are deliberately not dimmed themselves — when you hold a
// button in a dark room you want to be able to read the thing.
const uint16_t CARD_BG = 0x18E3;      // near-black grey
const uint16_t CARD_BORDER = 0x8410;  // mid grey
const uint16_t TEXT_PRIMARY = 0xFFFF;
const uint16_t TEXT_MUTED = 0xAD55;
const uint16_t ACCENT = 0xFD59;  // warm pink, matches the frame's mood

void drawCard(int16_t x, int16_t y, int16_t w, int16_t h) {
  g_gfx->fillRoundRect(x, y, w, h, 16, CARD_BG);
  g_gfx->drawRoundRect(x, y, w, h, 16, CARD_BORDER);
}

// Draws text horizontally centred on `cx`. The built-in GFX font is 6x8 per
// unit of text size.
void drawCentered(const char *text, int16_t cx, int16_t y, uint8_t size,
                  uint16_t color) {
  int16_t w = (int16_t)(strlen(text) * 6 * size);
  g_gfx->setTextSize(size);
  g_gfx->setTextColor(color);
  g_gfx->setCursor(cx - w / 2, y);
  g_gfx->print(text);
}

// Escapes the characters that are special inside a WIFI: QR payload.
String escapeWifiField(const char *s) {
  String out;
  for (const char *p = s; *p; p++) {
    if (*p == '\\' || *p == ';' || *p == ',' || *p == ':' || *p == '"') {
      out += '\\';
    }
    out += *p;
  }
  return out;
}

}  // namespace

bool begin() {
  g_panel = new Arduino_ESP32RGBPanel(
      PIN_LCD_DE, PIN_LCD_VSYNC, PIN_LCD_HSYNC, PIN_LCD_PCLK,
      PIN_LCD_R[0], PIN_LCD_R[1], PIN_LCD_R[2], PIN_LCD_R[3], PIN_LCD_R[4],
      PIN_LCD_G[0], PIN_LCD_G[1], PIN_LCD_G[2], PIN_LCD_G[3], PIN_LCD_G[4],
      PIN_LCD_G[5],
      PIN_LCD_B[0], PIN_LCD_B[1], PIN_LCD_B[2], PIN_LCD_B[3], PIN_LCD_B[4],
      /* hsync_polarity */ 0, /* hsync_front_porch */ 8,
      /* hsync_pulse_width */ 4, /* hsync_back_porch */ 8,
      /* vsync_polarity */ 0, /* vsync_front_porch */ 8,
      /* vsync_pulse_width */ 4, /* vsync_back_porch */ 8,
      /* pclk_active_neg */ 1, /* prefer_speed */ LCD_PCLK_HZ);

  g_gfx = new Arduino_RGB_Display(SCREEN_W, SCREEN_H, g_panel, 0, true);
  if (!g_gfx->begin()) {
    Serial.println("[display] panel init failed");
    return false;
  }

  g_fb = g_gfx->getFramebuffer();
  if (g_fb == nullptr) {
    Serial.println("[display] no framebuffer — is PSRAM enabled?");
    return false;
  }

  const size_t bytes = SCREEN_PIXELS * sizeof(uint16_t);
  g_front = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  g_back = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (g_front == nullptr || g_back == nullptr) {
    Serial.println("[display] could not allocate PSRAM image buffers");
    return false;
  }
  memset(g_front, 0, bytes);
  memset(g_back, 0, bytes);

  rebuildLuts();
  g_gfx->fillScreen(0x0000);
  return true;
}

uint16_t *backBuffer() { return g_back; }

void swapBuffers() {
  uint16_t *tmp = g_front;
  g_front = g_back;
  g_back = tmp;
  blit(g_front);
}

void repaint() { blit(g_front); }

void fillFront(uint16_t color) {
  if (g_front == nullptr) return;
  for (uint32_t i = 0; i < SCREEN_PIXELS; i++) g_front[i] = color;
  blit(g_front);
}

void setBrightness(uint8_t brightness) {
  if (brightness == g_brightness) return;
  g_brightness = brightness;
  rebuildLuts();
  blit(g_front);
}

uint8_t brightness() { return g_brightness; }

void setPanelOn(bool on) {
  if (on == g_panelOn) return;
  g_panelOn = on;
  ch422g::setDisplayEnabled(on);
}

bool panelOn() { return g_panelOn; }

void drawWifiQr(const char *ssid, const char *password, const char *url) {
  // Standard Wi-Fi provisioning payload. Scanning this joins the network; the
  // captive portal then opens the upload page by itself.
  String payload = "WIFI:T:WPA;S:" + escapeWifiField(ssid) + ";P:" +
                   escapeWifiField(password) + ";;";

  // Version 6 is 41x41 modules and holds ~134 bytes at ECC medium — ample for a
  // WIFI: payload. The buffer size is computed here rather than via
  // qrcode_getBufferSize(), which is a function call and would make this a
  // variable-length array.
  constexpr uint8_t version = 6;
  constexpr uint16_t modules = version * 4 + 17;
  constexpr uint16_t bufSize = (modules * modules + 7) / 8;

  QRCode qr;
  uint8_t qrData[bufSize];
  if (qrcode_initText(&qr, qrData, version, ECC_MEDIUM, payload.c_str()) < 0) {
    drawMessage("Upload photos", "Join the Wi-Fi network below", url);
    return;
  }

  const int16_t scale = 8;
  const int16_t quiet = 4 * scale;  // QR spec wants 4 modules of margin
  const int16_t qrPx = qr.size * scale;
  const int16_t cardW = 460;
  const int16_t cardH = qrPx + quiet * 2 + 130;
  const int16_t cardX = (SCREEN_W - cardW) / 2;
  const int16_t cardY = (SCREEN_H - cardH) / 2;

  drawCard(cardX, cardY, cardW, cardH);
  drawCentered("Add photos", SCREEN_W / 2, cardY + 22, 3, TEXT_PRIMARY);

  // White quiet zone, then the modules. Scanners need the light margin.
  const int16_t qrX = (SCREEN_W - qrPx) / 2;
  const int16_t qrY = cardY + 62;
  g_gfx->fillRect(qrX - quiet, qrY - quiet, qrPx + quiet * 2,
                  qrPx + quiet * 2, 0xFFFF);
  for (uint8_t y = 0; y < qr.size; y++) {
    for (uint8_t x = 0; x < qr.size; x++) {
      if (qrcode_getModule(&qr, x, y)) {
        g_gfx->fillRect(qrX + x * scale, qrY + y * scale, scale, scale, 0x0000);
      }
    }
  }

  const int16_t textY = qrY + qrPx + quiet + 14;
  drawCentered(ssid, SCREEN_W / 2, textY, 2, ACCENT);
  drawCentered(url, SCREEN_W / 2, textY + 26, 2, TEXT_MUTED);
  drawCentered("scan to join, then upload", SCREEN_W / 2, textY + 52, 1,
               TEXT_MUTED);
}

void drawIntervalMenu(const char *label, uint8_t index, uint8_t count) {
  const int16_t cardW = 460;
  const int16_t cardH = 190;
  const int16_t cardX = (SCREEN_W - cardW) / 2;
  const int16_t cardY = (SCREEN_H - cardH) / 2;

  drawCard(cardX, cardY, cardW, cardH);
  drawCentered("Time per photo", SCREEN_W / 2, cardY + 24, 2, TEXT_MUTED);
  drawCentered(label, SCREEN_W / 2, cardY + 62, 4, TEXT_PRIMARY);

  // One dot per option, the selected one filled and accented.
  const int16_t spacing = 22;
  const int16_t dotsW = (count - 1) * spacing;
  const int16_t dotY = cardY + 122;
  for (uint8_t i = 0; i < count; i++) {
    int16_t cx = SCREEN_W / 2 - dotsW / 2 + i * spacing;
    if (i == index) {
      g_gfx->fillCircle(cx, dotY, 6, ACCENT);
    } else {
      g_gfx->drawCircle(cx, dotY, 4, CARD_BORDER);
    }
  }

  drawCentered("tap either button to change", SCREEN_W / 2, cardY + 150, 1,
               TEXT_MUTED);
}

void drawMessage(const char *title, const char *line1, const char *line2) {
  const int16_t cardW = 520;
  const int16_t cardH = 170;
  const int16_t cardX = (SCREEN_W - cardW) / 2;
  const int16_t cardY = (SCREEN_H - cardH) / 2;

  drawCard(cardX, cardY, cardW, cardH);
  drawCentered(title, SCREEN_W / 2, cardY + 32, 3, TEXT_PRIMARY);
  if (line1 && *line1) {
    drawCentered(line1, SCREEN_W / 2, cardY + 80, 2, TEXT_MUTED);
  }
  if (line2 && *line2) {
    drawCentered(line2, SCREEN_W / 2, cardY + 112, 2, ACCENT);
  }
}

}  // namespace display

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

// Draws text with its left edge at `x`. The built-in GFX font is 6x8 pixels per
// unit of text size, and the cursor sets the glyph's top-left corner.
void drawLeft(const char *text, int16_t x, int16_t y, uint8_t size,
              uint16_t color) {
  g_gfx->setTextSize(size);
  g_gfx->setTextColor(color);
  g_gfx->setCursor(x, y);
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

// One text row in the column beside a QR code.
struct QrRow {
  const char *text;
  uint8_t size;
  uint16_t color;
  int16_t gapAfter;
};

// Shared layout for both QR cards. Landscape: code on the left, rows on the
// right. Stacking them vertically does not fit — 41 modules at a readable scale
// plus several lines of text is taller than 480 px, which is exactly how this
// used to end up clipped at the top and bottom.
void drawQrCard(const String &payload, const QrRow *rows, uint8_t rowCount,
                const char *fallbackTitle, const char *fallbackLine) {
  // Version 6 is 41x41 modules and holds ~134 bytes at ECC medium, ample for
  // either payload. Size is computed rather than taken from
  // qrcode_getBufferSize(), which is a function and would make this a VLA.
  constexpr uint8_t version = 6;
  constexpr uint16_t modules = version * 4 + 17;
  constexpr uint16_t bufSize = (modules * modules + 7) / 8;

  QRCode qr;
  uint8_t qrData[bufSize];
  if (qrcode_initText(&qr, qrData, version, ECC_MEDIUM, payload.c_str()) < 0) {
    drawMessage(fallbackTitle, fallbackLine, "");
    return;
  }

  const int16_t cardH = 360;
  const int16_t cardW = 640;
  const int16_t cardX = (SCREEN_W - cardW) / 2;
  const int16_t cardY = (SCREEN_H - cardH) / 2;
  const int16_t pad = 22;

  // Derive the module scale from the height actually available rather than
  // hardcoding it, so the card can never overflow the panel again. The +8 covers
  // the 4 modules of quiet zone the QR spec requires on each side.
  const int16_t scale = (cardH - pad * 2) / (qr.size + 8);
  if (scale < 2) {
    drawMessage(fallbackTitle, fallbackLine, "");
    return;
  }

  const int16_t quiet = scale * 4;
  const int16_t block = (int16_t)(qr.size * scale) + quiet * 2;
  const int16_t blockX = cardX + pad;
  const int16_t blockY = cardY + (cardH - block) / 2;

  drawCard(cardX, cardY, cardW, cardH);

  // White quiet zone, then the modules. Scanners need the light margin.
  g_gfx->fillRect(blockX, blockY, block, block, 0xFFFF);
  const int16_t qrX = blockX + quiet;
  const int16_t qrY = blockY + quiet;
  for (uint8_t y = 0; y < qr.size; y++) {
    for (uint8_t x = 0; x < qr.size; x++) {
      if (qrcode_getModule(&qr, x, y)) {
        g_gfx->fillRect(qrX + x * scale, qrY + y * scale, scale, scale, 0x0000);
      }
    }
  }

  // Centre the column vertically rather than letting it ride high with dead
  // space underneath.
  int16_t stackH = 0;
  for (uint8_t i = 0; i < rowCount; i++) {
    stackH += (int16_t)(8 * rows[i].size) + rows[i].gapAfter;
  }

  const int16_t tx = blockX + block + 28;
  int16_t ty = cardY + (cardH - stackH) / 2;
  for (uint8_t i = 0; i < rowCount; i++) {
    if (rows[i].text && rows[i].text[0]) {
      drawLeft(rows[i].text, tx, ty, rows[i].size, rows[i].color);
    }
    ty += (int16_t)(8 * rows[i].size) + rows[i].gapAfter;
  }
}

void drawWifiQr(const char *ssid, const char *password, const char *url) {
  // Standard Wi-Fi provisioning payload: scanning it joins the network, and the
  // captive portal then opens the page by itself.
  String payload = "WIFI:T:WPA;S:" + escapeWifiField(ssid) + ";P:" +
                   escapeWifiField(password) + ";;";

  // The password is spelled out as well as encoded, so a phone that will not
  // scan can still be joined by hand.
  const QrRow rows[] = {
      {"Add photos", 3, TEXT_PRIMARY, 28},
      {"NETWORK", 1, TEXT_MUTED, 6},
      {ssid, 2, ACCENT, 24},
      {"PASSWORD", 1, TEXT_MUTED, 6},
      {password, 2, ACCENT, 24},
      {"THEN OPEN", 1, TEXT_MUTED, 6},
      {url, 2, TEXT_PRIMARY, 22},
      {"scan to join automatically", 1, TEXT_MUTED, 0},
  };
  drawQrCard(payload, rows, sizeof(rows) / sizeof(rows[0]), "Add photos", ssid);
}

void drawUrlQr(const char *url, const char *network, const char *alsoAt) {
  // Already on her network, so the phone is too — the code is just the address.
  const QrRow rows[] = {
      {"Add photos", 3, TEXT_PRIMARY, 30},
      {"SCAN, OR VISIT", 1, TEXT_MUTED, 6},
      {url, 2, ACCENT, 24},
      {"ALSO AT", 1, TEXT_MUTED, 6},
      {alsoAt, 2, TEXT_PRIMARY, 26},
      {"ON", 1, TEXT_MUTED, 6},
      {network, 2, TEXT_MUTED, 0},
  };
  drawQrCard(String(url), rows, sizeof(rows) / sizeof(rows[0]), "Add photos",
             url);
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

void drawNote(const char *from, const char *text) {
  const int16_t cardW = 600;
  const int16_t cardX = (SCREEN_W - cardW) / 2;
  const int16_t pad = 28;
  const uint8_t size = 3;
  const int16_t charW = 6 * size;
  const int16_t lineH = 8 * size + 8;
  const int16_t maxChars = (cardW - pad * 2) / charW;

  // Wrap on whitespace, breaking mid-word only when a single word is longer
  // than the card is wide.
  String lines[NOTE_MAX_LINES];
  uint8_t count = 0;
  String rest = text;
  rest.trim();

  while (rest.length() > 0 && count < NOTE_MAX_LINES) {
    if ((int16_t)rest.length() <= maxChars) {
      lines[count++] = rest;
      break;
    }
    int cut = -1;
    for (int16_t i = maxChars; i > 0; i--) {
      if (rest[i] == ' ') {
        cut = i;
        break;
      }
    }
    if (cut <= 0) cut = maxChars;  // one very long word
    lines[count++] = rest.substring(0, cut);
    rest = rest.substring(cut);
    rest.trim();
  }

  const int16_t textH = count * lineH;
  const int16_t cardH = pad * 2 + 34 + textH;
  const int16_t cardY = (SCREEN_H - cardH) / 2;

  drawCard(cardX, cardY, cardW, cardH);

  String heading = String("From ") + from;
  drawCentered(heading.c_str(), SCREEN_W / 2, cardY + pad, 2, ACCENT);

  int16_t y = cardY + pad + 34;
  for (uint8_t i = 0; i < count; i++) {
    drawCentered(lines[i].c_str(), SCREEN_W / 2, y, size, TEXT_PRIMARY);
    y += lineH;
  }
}

}  // namespace display

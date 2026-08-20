#include "webui.h"

#include <DNSServer.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "playlist.h"
#include "storage.h"
#include "telegram.h"
#include "web_page.h"
#include "wifimgr.h"

namespace webui {
namespace {

WebServer g_server(80);
DNSServer g_dns;
Hooks g_hooks;

// The radio belongs to wifimgr; this module only serves HTTP, plus DNS when we
// are the network and need the captive portal to fire.
bool g_dnsRunning = false;

// Upload state. Only one upload is ever in flight — the server is synchronous.
File g_file;
String g_path;
bool g_ok = false;
uint32_t g_bytes = 0;

// A cover-cropped 800x480 JPEG at quality 0.88 lands around 100-200 KB. This is
// a generous ceiling that still stops a hand-crafted request filling the card.
const uint32_t MAX_UPLOAD_BYTES = 2UL * 1024 * 1024;

void handleUploadInto(const char *dir) {
  HTTPUpload &up = g_server.upload();

  switch (up.status) {
    case UPLOAD_FILE_START: {
      g_ok = false;
      g_bytes = 0;
      String safe = storage::sanitizeName(up.filename);
      if (safe.length() == 0 || !storage::mounted()) return;

      g_path = String(dir) + "/" + safe;
      SD.remove(g_path);
      g_file = SD.open(g_path, FILE_WRITE);
      g_ok = (bool)g_file;
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!g_ok || !g_file) return;
      g_bytes += up.currentSize;
      if (g_bytes > MAX_UPLOAD_BYTES) {
        g_ok = false;
        g_file.close();
        SD.remove(g_path);
        return;
      }
      if (g_file.write(up.buf, up.currentSize) != up.currentSize) {
        g_ok = false;
        g_file.close();
        SD.remove(g_path);
      }
      break;
    }

    case UPLOAD_FILE_END:
      if (g_file) g_file.close();
      break;

    case UPLOAD_FILE_ABORTED:
      if (g_file) g_file.close();
      SD.remove(g_path);
      g_ok = false;
      break;
  }
}

void finishUpload(bool notify) {
  if (g_ok) {
    if (notify && g_hooks.onPhotosChanged) g_hooks.onPhotosChanged();
    g_server.send(200, "text/plain", "ok");
  } else {
    g_server.send(500, "text/plain", "write failed");
  }
}

void routeIndex() { g_server.send_P(200, "text/html", INDEX_HTML); }

// Filenames come off a FAT directory listing, so they are not guaranteed to be
// free of characters that would break a JSON string.
String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 2);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c < 0x20) continue;  // drop control characters outright
    out += c;
  }
  return out;
}

void routeStatus() {
  float lux = g_hooks.getLux ? g_hooks.getLux() : -1.0f;

  String json = "{";
  json += "\"count\":" + String(playlist::count());
  json += ",\"position\":" + String(playlist::position());
  json += ",\"current\":\"" + jsonEscape(playlist::current()) + "\"";
  json += ",\"interval_index\":" + String(storage::settings().intervalIndex);
  json += ",\"brightness\":" + String(display::brightness());
  json += ",\"panel_on\":" + String(display::panelOn() ? "true" : "false");
  json += ",\"lux\":" + String(lux, 1);
  json += ",\"portal\":" +
          String(wifimgr::mode() == wifimgr::Mode::Portal ? "true" : "false");
  json += ",\"network\":\"" + jsonEscape(wifimgr::ssid()) + "\"";
  json += ",\"online\":" + String(wifimgr::online() ? "true" : "false");
  json += ",\"tg_on\":" + String(telegram::configured() ? "true" : "false");
  json += ",\"tg_allowed\":" + String(telegram::allowedCount());
  json += ",\"tg_pending\":\"" +
          (telegram::pendingChatId() == 0
               ? String("")
               : String((long long)telegram::pendingChatId())) +
          "\"";
  json += ",\"tg_pending_name\":\"" + jsonEscape(telegram::pendingName()) + "\"";
  json += ",\"calibrated\":" +
          String(storage::settings().calibrated ? "true" : "false");
  json += ",\"lux_bright\":" + String(storage::settings().luxBright, 1);
  json += ",\"lux_dark\":" + String(storage::settings().luxDark, 2);
  json += ",\"used_mb\":" + String((uint32_t)(storage::cardUsedBytes() / 1048576));
  json += ",\"total_mb\":" + String((uint32_t)(storage::cardSizeBytes() / 1048576));
  json += "}";

  g_server.send(200, "application/json", json);
}

void routePhotos() {
  static String names[MAX_PHOTOS];
  uint16_t n = 0;
  storage::listPhotos(names, MAX_PHOTOS, &n);

  // Reserve up front — appending 500 short names one at a time otherwise
  // reallocates repeatedly and chews through the heap.
  String json;
  json.reserve((size_t)n * 16 + 2);
  json = "[";
  for (uint16_t i = 0; i < n; i++) {
    if (i) json += ',';
    json += '"';
    json += jsonEscape(names[i]);
    json += '"';
  }
  json += ']';

  g_server.send(200, "application/json", json);
}

// Serves a JPEG out of `dir`. Used for both the gallery thumbnails and the
// full-size image behind a tap.
void serveImage(const char *dir) {
  String safe = storage::sanitizeName(g_server.arg("name"));
  if (safe.length() == 0) {
    g_server.send(400, "text/plain", "bad name");
    return;
  }

  String path = String(dir) + "/" + safe;
  File f = SD.open(path, FILE_READ);
  if (!f) {
    // Photos copied onto the card by hand have no thumbnail; the page falls
    // back to the full-size image when this 404s.
    g_server.send(404, "text/plain", "not found");
    return;
  }

  g_server.sendHeader("Cache-Control", "max-age=86400");
  g_server.streamFile(f, "image/jpeg");
  f.close();
}

void routeShow() {
  String safe = storage::sanitizeName(g_server.arg("name"));
  if (safe.length() == 0 || g_hooks.onShow == nullptr ||
      !g_hooks.onShow(safe.c_str())) {
    g_server.send(404, "text/plain", "not found");
    return;
  }
  g_server.send(200, "text/plain", "ok");
}

void routeDelete() {
  String name = g_server.arg("name");
  if (!storage::deletePhoto(name)) {
    g_server.send(404, "text/plain", "not found");
    return;
  }
  if (g_hooks.onPhotosChanged) g_hooks.onPhotosChanged();
  g_server.send(200, "text/plain", "ok");
}

void routeInterval() {
  int index = g_server.arg("index").toInt();
  if (index < 0 || index >= INTERVAL_OPTION_COUNT) {
    g_server.send(400, "text/plain", "bad index");
    return;
  }
  storage::settings().intervalIndex = (uint8_t)index;
  storage::saveSettings();
  g_server.send(200, "text/plain", "ok");
}

void routeNext() {
  if (g_hooks.onNext) g_hooks.onNext();
  g_server.send(200, "text/plain", "ok");
}

void routePrev() {
  if (g_hooks.onPrev) g_hooks.onPrev();
  g_server.send(200, "text/plain", "ok");
}

void routeScan() {
  g_server.send(200, "application/json", wifimgr::scanJson());
}

void routeJoin() {
  String ssid = g_server.arg("ssid");
  String password = g_server.arg("password");

  if (!wifimgr::saveCredentials(ssid, password)) {
    g_server.send(400, "text/plain", "bad credentials");
    return;
  }

  // Answer before restarting, so the phone sees the confirmation rather than a
  // dropped connection. Joining a new network means tearing down this AP, and
  // rebooting is far more predictable than reconfiguring the radio underneath a
  // live HTTP response.
  g_server.send(200, "text/plain", "ok");
  delay(400);
  Serial.println("[wifi] credentials saved, restarting to join");
  ESP.restart();
}

void routeTelegram() {
  if (!telegram::setToken(g_server.arg("token"))) {
    g_server.send(400, "text/plain", "that does not look like a bot token");
    return;
  }
  // Storing a token mid-run is fine — polling picks it up on the next pass — but
  // a restart is the simplest way to get a clean TLS client either way.
  g_server.send(200, "text/plain", "ok");
  delay(400);
  ESP.restart();
}

void routeTelegramAllow() {
  if (!telegram::allowPending()) {
    g_server.send(400, "text/plain", "nobody waiting");
    return;
  }
  g_server.send(200, "text/plain", "ok");
}

void routeCalibrate() {
  if (g_hooks.onCalibrate) g_hooks.onCalibrate();
  g_server.send(200, "text/plain", "ok");
}

void routeForget() {
  wifimgr::forgetCredentials();
  g_server.send(200, "text/plain", "ok");
  delay(400);
  ESP.restart();
}

// Anything we do not recognise gets bounced to the root. This is what makes the
// captive-portal sheet pop open by itself once the phone joins.
void routeNotFound() {
  g_server.sendHeader("Location", String(wifimgr::url()) + "/", true);
  g_server.send(302, "text/plain", "");
}

}  // namespace

bool begin(const Hooks &hooks) {
  g_hooks = hooks;

  g_server.on("/", HTTP_GET, routeIndex);
  g_server.on("/api/status", HTTP_GET, routeStatus);
  g_server.on("/api/photos", HTTP_GET, routePhotos);
  g_server.on("/api/thumb", HTTP_GET, []() { serveImage(DIR_THUMBS); });
  g_server.on("/api/photo", HTTP_GET, []() { serveImage(DIR_PHOTOS); });
  g_server.on("/api/show", HTTP_POST, routeShow);
  g_server.on("/api/delete", HTTP_POST, routeDelete);
  g_server.on("/api/interval", HTTP_POST, routeInterval);
  g_server.on("/api/next", HTTP_POST, routeNext);
  g_server.on("/api/prev", HTTP_POST, routePrev);
  g_server.on("/api/scan", HTTP_GET, routeScan);
  g_server.on("/api/join", HTTP_POST, routeJoin);
  g_server.on("/api/forget", HTTP_POST, routeForget);
  g_server.on("/api/telegram", HTTP_POST, routeTelegram);
  g_server.on("/api/telegram/allow", HTTP_POST, routeTelegramAllow);
  g_server.on("/api/calibrate", HTTP_POST, routeCalibrate);

  g_server.on(
      "/api/upload", HTTP_POST, []() { finishUpload(true); },
      []() { handleUploadInto(DIR_PHOTOS); });
  g_server.on(
      "/api/thumbup", HTTP_POST, []() { finishUpload(false); },
      []() { handleUploadInto(DIR_THUMBS); });

  g_server.onNotFound(routeNotFound);
  g_server.begin();

  // Resolving every hostname to us is what makes the captive-portal sheet open
  // by itself. Only meaningful while we are the network — on her Wi-Fi it would
  // be hijacking DNS for every device in the house.
  if (wifimgr::mode() == wifimgr::Mode::Portal) {
    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    g_dns.start(53, "*", wifimgr::ip());
    g_dnsRunning = true;
  }

  Serial.printf("[http] serving at %s\n", wifimgr::url());
  return true;
}

void loop() {
  bool portal = (wifimgr::mode() == wifimgr::Mode::Portal);

  // wifimgr can drop us into portal mode long after boot, so pick DNS up then.
  if (portal && !g_dnsRunning) {
    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    g_dns.start(53, "*", wifimgr::ip());
    g_dnsRunning = true;
  } else if (!portal && g_dnsRunning) {
    g_dns.stop();
    g_dnsRunning = false;
  }

  if (g_dnsRunning) g_dns.processNextRequest();
  g_server.handleClient();
}

IPAddress ip() { return wifimgr::ip(); }

const char *url() { return wifimgr::url(); }

}  // namespace webui

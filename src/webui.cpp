#include "webui.h"

#include <DNSServer.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "playlist.h"
#include "storage.h"
#include "web_page.h"

namespace webui {
namespace {

WebServer g_server(80);
DNSServer g_dns;
Hooks g_hooks;

IPAddress g_apIp(192, 168, 4, 1);
char g_url[32] = "http://192.168.4.1";

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

// Anything we do not recognise gets bounced to the root. This is what makes the
// captive-portal sheet pop open by itself once the phone joins.
void routeNotFound() {
  g_server.sendHeader("Location", String(g_url) + "/", true);
  g_server.send(302, "text/plain", "");
}

}  // namespace

bool begin(const Hooks &hooks) {
  g_hooks = hooks;

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(g_apIp, g_apIp, IPAddress(255, 255, 255, 0));
  bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, /* hidden */ false,
                        AP_MAX_CLIENTS);
  if (!ok) {
    Serial.println("[wifi] could not start the access point");
    return false;
  }
  WiFi.setSleep(false);

  g_apIp = WiFi.softAPIP();
  snprintf(g_url, sizeof(g_url), "http://%s", g_apIp.toString().c_str());

  // Resolve every hostname to us, so captive-portal detection triggers.
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", g_apIp);

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

  g_server.on(
      "/api/upload", HTTP_POST, []() { finishUpload(true); },
      []() { handleUploadInto(DIR_PHOTOS); });
  g_server.on(
      "/api/thumbup", HTTP_POST, []() { finishUpload(false); },
      []() { handleUploadInto(DIR_THUMBS); });

  g_server.onNotFound(routeNotFound);
  g_server.begin();

  Serial.printf("[wifi] \"%s\" up at %s\n", AP_SSID, g_url);
  return true;
}

void loop() {
  g_dns.processNextRequest();
  g_server.handleClient();
}

IPAddress ip() { return g_apIp; }

const char *url() { return g_url; }

}  // namespace webui

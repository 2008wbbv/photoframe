#include "telegram.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <SD.h>
#include <esp_random.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "storage.h"
#include "wifimgr.h"

namespace telegram {
namespace {

const char *const HOST = "api.telegram.org";

Preferences g_prefs;

// Kept in NVS rather than in the firmware. A token compiled into a source file
// ends up in git history the first time the repo is pushed and stays there even
// if the line is later removed — and anyone holding it controls the bot.
String g_token;
int64_t g_allowedIds[TELEGRAM_MAX_ALLOWED];
uint8_t g_allowedCount = 0;

// Last sender we turned away, so the setup page can offer to allow them without
// anybody having to read a serial console.
int64_t g_pendingId = 0;
String g_pendingName;

int32_t g_offset = 0;  // highest update_id seen, so updates are not re-fetched
uint32_t g_lastPoll = 0;

String g_message;
String g_sender;
uint32_t g_messageAt = 0;
bool g_pending = false;

// Shared across requests so the TLS session can be reused rather than
// renegotiated for every poll, which matters on a device this size.
WiFiClientSecure *g_tls = nullptr;

bool allowed(int64_t chatId) {
  for (uint8_t i = 0; i < g_allowedCount; i++) {
    if (g_allowedIds[i] == chatId) return true;
  }
  return false;
}

void loadAllowed() {
  g_allowedCount = 0;
  String packed = g_prefs.getString("tgids", "");
  int from = 0;
  while (from < (int)packed.length() && g_allowedCount < TELEGRAM_MAX_ALLOWED) {
    int comma = packed.indexOf(',', from);
    String piece = comma < 0 ? packed.substring(from) : packed.substring(from, comma);
    piece.trim();
    if (piece.length() > 0) {
      g_allowedIds[g_allowedCount++] = (int64_t)atoll(piece.c_str());
    }
    if (comma < 0) break;
    from = comma + 1;
  }
}

void saveAllowed() {
  String packed;
  for (uint8_t i = 0; i < g_allowedCount; i++) {
    if (i) packed += ',';
    packed += String((long long)g_allowedIds[i]);
  }
  g_prefs.putString("tgids", packed);
}

// Minimal JSON field extraction. A full parser would cost more flash and RAM
// than this deserves — the shapes we care about are fixed and shallow, and
// anything unexpected simply fails to match and is skipped.
String findString(const String &src, const String &key, int from = 0) {
  String needle = "\"" + key + "\":\"";
  int at = src.indexOf(needle, from);
  if (at < 0) return String();
  at += needle.length();

  String out;
  for (int i = at; i < (int)src.length(); i++) {
    char c = src[i];
    if (c == '\\') {  // keep escaped characters intact
      if (i + 1 < (int)src.length()) {
        char next = src[++i];
        switch (next) {
          case 'n': out += '\n'; break;
          case 't': out += ' '; break;
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          default: break;  // drop \uXXXX and friends rather than mangle them
        }
      }
      continue;
    }
    if (c == '"') break;
    out += c;
  }
  return out;
}

int64_t findNumber(const String &src, const String &key, int from = 0) {
  String needle = "\"" + key + "\":";
  int at = src.indexOf(needle, from);
  if (at < 0) return 0;
  at += needle.length();

  bool negative = false;
  if (at < (int)src.length() && src[at] == '-') {
    negative = true;
    at++;
  }

  int64_t value = 0;
  bool any = false;
  for (int i = at; i < (int)src.length(); i++) {
    char c = src[i];
    if (c < '0' || c > '9') break;
    value = value * 10 + (c - '0');
    any = true;
  }
  if (!any) return 0;
  return negative ? -value : value;
}

bool request(const String &path, String *response) {
  if (g_tls == nullptr) return false;

  HTTPClient http;
  http.setTimeout(TELEGRAM_HTTP_TIMEOUT_MS);
  http.setReuse(true);

  String url = "https://" + String(HOST) + path;
  if (!http.begin(*g_tls, url)) return false;

  int code = http.GET();
  bool ok = (code == 200);
  if (ok && response) {
    *response = http.getString();
  } else if (!ok) {
    Serial.printf("[telegram] HTTP %d on %s\n", code,
                  path.substring(0, 24).c_str());
  }
  http.end();
  return ok;
}

// Streams a file straight from Telegram onto the card, so a photo never has to
// fit in RAM all at once.
bool download(const String &filePath, const String &destPath) {
  if (g_tls == nullptr) return false;

  HTTPClient http;
  http.setTimeout(TELEGRAM_HTTP_TIMEOUT_MS);
  String url = "https://" + String(HOST) + "/file/bot" +
               g_token + "/" + filePath;
  if (!http.begin(*g_tls, url)) return false;

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[telegram] download failed, HTTP %d\n", code);
    http.end();
    return false;
  }

  int expected = http.getSize();
  if (expected > (int)TELEGRAM_MAX_PHOTO_BYTES) {
    Serial.printf("[telegram] photo is %d bytes, over the limit\n", expected);
    http.end();
    return false;
  }

  SD.remove(destPath);
  File out = SD.open(destPath, FILE_WRITE);
  if (!out) {
    http.end();
    return false;
  }

  int written = http.writeToStream(&out);
  out.close();
  http.end();

  if (written <= 0) {
    SD.remove(destPath);
    Serial.println("[telegram] wrote nothing, discarding");
    return false;
  }

  Serial.printf("[telegram] saved %d bytes to %s\n", written,
                destPath.c_str());
  return true;
}

// Generates a name in the same 8-character form the web UI uses, so everything
// on the card looks alike and stays inside 8.3.
String newPhotoName() {
  static const char *hex = "0123456789ABCDEF";
  String name;
  for (uint8_t i = 0; i < 8; i++) name += hex[esp_random() & 0x0F];
  return name + ".jpg";
}

// Picks the largest offered size that is still worth downloading. Telegram lists
// each photo at several resolutions, smallest first.
String bestPhotoFileId(const String &message) {
  int photoAt = message.indexOf("\"photo\":[");
  if (photoAt < 0) return String();

  int end = message.indexOf(']', photoAt);
  if (end < 0) return String();
  String array = message.substring(photoAt, end);

  // Walk every size, keeping the last one whose width still fits our budget.
  String chosen;
  int cursor = 0;
  while (true) {
    int entry = array.indexOf("{", cursor);
    if (entry < 0) break;
    int entryEnd = array.indexOf('}', entry);
    if (entryEnd < 0) break;

    String item = array.substring(entry, entryEnd);
    int64_t width = findNumber(item, "width");
    String id = findString(item, "file_id");

    if (id.length() > 0 && width <= (int64_t)TELEGRAM_MAX_PHOTO_WIDTH) {
      chosen = id;  // sizes ascend, so the last match is the biggest usable one
    }
    cursor = entryEnd + 1;
  }

  // Everything on offer was oversized; take the smallest rather than nothing.
  if (chosen.length() == 0) chosen = findString(array, "file_id");
  return chosen;
}

void reply(int64_t chatId, const String &text) {
  String encoded;
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (isalnum((int)c)) {
      encoded += c;
    } else if (c == ' ') {
      encoded += '+';
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
      encoded += buf;
    }
  }
  request("/bot" + g_token + "/sendMessage?chat_id=" +
              String((long long)chatId) + "&text=" + encoded,
          nullptr);
}

// Handles one update. Returns true if the playlist changed.
bool handleUpdate(const String &update, bool *messageArrived) {
  int64_t chatId = findNumber(update, "id");  // first id in "chat":{"id":...}
  int chatAt = update.indexOf("\"chat\":");
  if (chatAt >= 0) chatId = findNumber(update, "id", chatAt);

  String from = findString(update, "first_name");
  if (from.length() == 0) from = "Someone";

  if (!allowed(chatId)) {
    // Remembered rather than just dropped, so the setup page can offer to allow
    // this sender. Also printed, for anyone watching the console instead.
    g_pendingId = chatId;
    g_pendingName = from;
    Serial.printf("[telegram] message from unallowed chat id %lld (%s)\n",
                  (long long)chatId, from.c_str());
    return false;
  }

  // A photo, possibly with a caption.
  String fileId = bestPhotoFileId(update);
  if (fileId.length() > 0) {
    String meta;
    if (!request("/bot" + g_token + "/getFile?file_id=" +
                     fileId,
                 &meta)) {
      return false;
    }

    String remotePath = findString(meta, "file_path");
    if (remotePath.length() == 0) return false;

    String name = newPhotoName();
    if (!download(remotePath, String(DIR_PHOTOS) + "/" + name)) return false;

    String caption = findString(update, "caption");
    if (caption.length() > 0) {
      g_message = caption;
      g_sender = from;
      g_messageAt = millis();
      g_pending = true;
      if (messageArrived) *messageArrived = true;
    }

    reply(chatId, "Added to the frame.");
    Serial.printf("[telegram] photo from %s\n", from.c_str());
    return true;
  }

  // Plain text.
  String text = findString(update, "text");
  if (text.length() > 0) {
    if (text.length() > TELEGRAM_MAX_MESSAGE_CHARS) {
      text = text.substring(0, TELEGRAM_MAX_MESSAGE_CHARS);
    }
    g_message = text;
    g_sender = from;
    g_messageAt = millis();
    g_pending = true;
    if (messageArrived) *messageArrived = true;

    reply(chatId, "Showing on the frame.");
    Serial.printf("[telegram] message from %s: %s\n", from.c_str(),
                  text.c_str());
  }
  return false;
}

}  // namespace

bool configured() { return g_token.length() > 0; }

bool hasAllowedSenders() { return g_allowedCount > 0; }

int64_t pendingChatId() { return g_pendingId; }

String pendingName() { return g_pendingName; }

bool allowPending() {
  if (g_pendingId == 0 || g_allowedCount >= TELEGRAM_MAX_ALLOWED) return false;
  g_allowedIds[g_allowedCount++] = g_pendingId;
  saveAllowed();
  Serial.printf("[telegram] allowed chat id %lld\n", (long long)g_pendingId);
  g_pendingId = 0;
  g_pendingName = "";
  return true;
}

uint8_t allowedCount() { return g_allowedCount; }

bool setToken(const String &token) {
  // Telegram tokens look like 8123456:AAH... — enough of a shape to catch a
  // paste of the wrong thing entirely.
  if (token.length() > 0 && (token.length() < 20 || token.indexOf(':') < 0)) {
    return false;
  }
  g_token = token;
  g_prefs.putString("tgtoken", token);
  Serial.println(token.length() ? "[telegram] token stored"
                                : "[telegram] token cleared");
  return true;
}

void begin() {
  g_prefs.begin(NVS_NAMESPACE, /* readOnly */ false);
  g_token = g_prefs.getString("tgtoken", "");
  loadAllowed();

  if (!configured()) {
    Serial.println("[telegram] no bot token — remote sending disabled");
    return;
  }

  g_tls = new WiFiClientSecure();
  if (g_tls == nullptr) return;

  // Not validating the certificate chain, deliberately. Pinning a root CA is
  // stronger, but Telegram rotates issuers and an expired pin would silently
  // kill a frame that is sitting on someone's shelf with no way to update it.
  // The exposure is that someone already inside the network path could read the
  // bot token; the allowlist limits what that buys them.
  g_tls->setInsecure();
  g_tls->setTimeout(TELEGRAM_HTTP_TIMEOUT_MS / 1000);

  Serial.printf("[telegram] ready, %u allowed sender(s)\n",
                (unsigned)g_allowedCount);
}

bool poll() {
  if (!configured()) return false;
  if (!wifimgr::online()) return false;

  // The token can arrive from the setup page long after boot, so the TLS client
  // is created on first use rather than only in begin().
  if (g_tls == nullptr) {
    g_tls = new WiFiClientSecure();
    if (g_tls == nullptr) return false;
    g_tls->setInsecure();
    g_tls->setTimeout(TELEGRAM_HTTP_TIMEOUT_MS / 1000);
  }

  uint32_t now = millis();
  if (now - g_lastPoll < TELEGRAM_POLL_INTERVAL_MS) return false;
  g_lastPoll = now;

  String body;
  String path = "/bot" + g_token +
                "/getUpdates?limit=1&timeout=0&allowed_updates=[\"message\"]";
  if (g_offset > 0) path += "&offset=" + String(g_offset);

  if (!request(path, &body)) return false;
  if (body.indexOf("\"result\":[]") >= 0) return false;  // nothing waiting

  int64_t updateId = findNumber(body, "update_id");
  if (updateId == 0) return false;

  // Acknowledge before acting: if handling crashes or the photo is rejected we
  // still move past this update rather than fetching it forever.
  g_offset = (int32_t)(updateId + 1);

  bool messageArrived = false;
  bool photosChanged = handleUpdate(body, &messageArrived);
  return photosChanged;
}

String lastMessage() { return g_message; }

String lastSender() { return g_sender; }

uint32_t lastMessageAt() { return g_messageAt; }

bool messagePending() { return g_pending; }

void clearMessage() { g_pending = false; }

}  // namespace telegram

#include "wifimgr.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

#include "config.h"

namespace wifimgr {
namespace {

Preferences g_prefs;
Mode g_mode = Mode::Booting;

char g_url[40] = "";
String g_ssid;
String g_password;

uint32_t g_lastCheck = 0;
uint32_t g_retryingSince = 0;
bool g_mdnsUp = false;

void setUrl(IPAddress addr) {
  snprintf(g_url, sizeof(g_url), "http://%s", addr.toString().c_str());
}

void startMdns() {
  if (g_mdnsUp) return;
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    g_mdnsUp = true;
    Serial.printf("[wifi] also reachable at http://%s.local\n", MDNS_HOSTNAME);
  }
}

// Brings up our own access point with the setup page on it.
void openPortal() {
  g_mode = Mode::Portal;
  g_mdnsUp = false;

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  IPAddress apIp(192, 168, 4, 1);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CLIENTS);
  WiFi.setSleep(false);

  setUrl(WiFi.softAPIP());
  Serial.printf("[wifi] portal up: \"%s\" at %s\n", AP_SSID, g_url);
}

// Tries to join the stored network. Blocks for up to WIFI_CONNECT_TIMEOUT_MS,
// which only happens at boot or after the network has been gone a long while.
bool joinStoredNetwork() {
  if (g_ssid.length() == 0) return false;

  Serial.printf("[wifi] joining \"%s\"\n", g_ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_ssid.c_str(), g_password.c_str());

  uint32_t started = millis();
  while (millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      g_mode = Mode::Station;
      setUrl(WiFi.localIP());
      Serial.printf("[wifi] joined \"%s\" at %s\n", g_ssid.c_str(), g_url);
      startMdns();
      return true;
    }
    delay(100);
  }

  Serial.printf("[wifi] could not join \"%s\"\n", g_ssid.c_str());
  return false;
}

}  // namespace

void begin() {
  g_prefs.begin(NVS_NAMESPACE, /* readOnly */ false);
  g_ssid = g_prefs.getString("ssid", "");
  g_password = g_prefs.getString("pass", "");

  if (!joinStoredNetwork()) openPortal();
}

void loop() {
  if (g_mode != Mode::Station) return;

  uint32_t now = millis();
  if (now - g_lastCheck < WIFI_CHECK_INTERVAL_MS) return;
  g_lastCheck = now;

  if (WiFi.status() == WL_CONNECTED) {
    g_retryingSince = 0;
    // The address can change across a DHCP lease renewal, so keep the URL the
    // QR code advertises in step with reality.
    String current = "http://" + WiFi.localIP().toString();
    if (current != g_url) setUrl(WiFi.localIP());
    startMdns();
    return;
  }

  // Dropped. The core retries on its own, so only step in once it has stayed
  // down long enough that the network is probably not coming back.
  if (g_retryingSince == 0) {
    g_retryingSince = now;
    Serial.println("[wifi] connection lost, waiting for it to come back");
    return;
  }

  if (now - g_retryingSince >= WIFI_GIVE_UP_MS) {
    Serial.println("[wifi] gone too long — reopening the setup portal");
    openPortal();
  }
}

Mode mode() { return g_mode; }

bool online() {
  return g_mode == Mode::Station && WiFi.status() == WL_CONNECTED;
}

IPAddress ip() {
  return g_mode == Mode::Station ? WiFi.localIP() : WiFi.softAPIP();
}

const char *url() { return g_url; }

const char *hostname() { return MDNS_HOSTNAME; }

String ssid() { return g_mode == Mode::Station ? g_ssid : String(); }

bool hasCredentials() { return g_ssid.length() > 0; }

bool saveCredentials(const String &ssid, const String &password) {
  if (ssid.length() == 0 || ssid.length() > 32) return false;
  if (password.length() > 63) return false;

  g_prefs.putString("ssid", ssid);
  g_prefs.putString("pass", password);
  Serial.printf("[wifi] stored credentials for \"%s\"\n", ssid.c_str());
  return true;
}

void forgetCredentials() {
  g_prefs.remove("ssid");
  g_prefs.remove("pass");
  g_ssid = "";
  g_password = "";
  Serial.println("[wifi] credentials cleared");
}

String scanJson() {
  Serial.println("[wifi] scanning");
  int found = WiFi.scanNetworks();

  // Mesh and extender setups advertise the same SSID several times, which would
  // otherwise fill the picker with duplicates. Track what has been listed using
  // a newline-delimited haystack so one name cannot match inside another.
  String seen = "\n";
  String json = "[";
  uint8_t listed = 0;

  for (int i = 0; i < found && listed < WIFI_SCAN_MAX_RESULTS; i++) {
    String name = WiFi.SSID(i);
    if (name.length() == 0) continue;
    if (seen.indexOf("\n" + name + "\n") >= 0) continue;
    seen += name + "\n";

    String safe;
    for (size_t c = 0; c < name.length(); c++) {
      char ch = name[c];
      if ((uint8_t)ch < 0x20) continue;
      if (ch == '"' || ch == '\\') safe += '\\';
      safe += ch;
    }

    if (listed) json += ',';
    json += "{\"ssid\":\"" + safe + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
            ",\"open\":" +
            (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
    listed++;
  }
  json += ']';

  WiFi.scanDelete();
  return json;
}

}  // namespace wifimgr

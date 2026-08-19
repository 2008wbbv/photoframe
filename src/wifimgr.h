// Wi-Fi bring-up and credential storage.
//
// The frame needs to be on a real network to reach the internet, but it also has
// to be configurable by someone who has never seen a serial console. So:
//
//   credentials stored  ->  join that network (Station mode)
//   none, or join fails ->  host "Rachel's Frame" and serve the setup page
//
// Credentials live in NVS rather than in the firmware, which means her password
// is typed in by her, on her own phone, and never passes through a source file
// or through whoever built the thing.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace wifimgr {

enum class Mode : uint8_t {
  Booting,
  Station,  // joined her network; has internet
  Portal,   // hosting our own AP with the setup page
};

// Reads stored credentials and either joins or opens the portal.
void begin();

// Call every loop. Watches for a dropped connection and retries, falling back
// to the portal if the network has gone for good.
void loop();

Mode mode();
bool online();  // Station mode and actually associated

// What the QR code should point at, which differs by mode: a LAN URL when we
// are on her network, and our own AP details when we are the network.
IPAddress ip();
const char *url();       // "http://192.168.1.42"
const char *hostname();  // "photoframe" -> photoframe.local

// The network we are joined to, or empty in portal mode.
String ssid();

// Stores credentials and reboots into Station mode. Called by the setup page.
bool saveCredentials(const String &ssid, const String &password);

// Forgets the stored network and reopens the portal.
void forgetCredentials();

bool hasCredentials();

// Scans for networks, best signal first, as JSON for the setup page.
String scanJson();

}  // namespace wifimgr

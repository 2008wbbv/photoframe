// Access point, captive portal and upload server.
//
// Deliberately the synchronous WebServer rather than ESPAsyncWebServer: it runs
// on the main task, so SD writes from an upload cannot race the slideshow's SD
// reads and no locking is needed. The cost is that the slideshow pauses while
// photos are being uploaded, which is fine — nobody is watching the frame while
// they are adding to it.

#pragma once

#include <IPAddress.h>
#include <stdint.h>

namespace webui {

// Hooks back into the main sketch, so this module does not need to know about
// the playlist or the light sensor.
struct Hooks {
  void (*onNext)() = nullptr;
  void (*onPrev)() = nullptr;
  void (*onPhotosChanged)() = nullptr;  // fired after an upload or delete
  bool (*onShow)(const char *name) = nullptr;  // jump to one photo from the gallery
  void (*onCalibrate)() = nullptr;  // re-run the first-boot wizard on the panel
  float (*getLux)() = nullptr;
};

bool begin(const Hooks &hooks);

// Call every loop to service DNS and HTTP.
void loop();

IPAddress ip();

// "http://192.168.4.1" — what gets printed under the QR code.
const char *url();

}  // namespace webui

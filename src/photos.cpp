#include "photos.h"

#include <JPEGDEC.h>
#include <SD.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "storage.h"

namespace photos {
namespace {

JPEGDEC g_jpeg;

// Where the decoder writes. Set up before each decode; the draw callback has to
// be a plain function, so these have to live at file scope.
uint16_t *g_dest = nullptr;
int16_t g_offsetX = 0;
int16_t g_offsetY = 0;

int drawCallback(JPEGDRAW *draw) {
  if (g_dest == nullptr) return 0;

  // iWidth is the block's stride; iWidthUsed is how much of it is real pixels,
  // which differs on the right-hand edge when the image is not a whole number
  // of MCUs wide. Photos from the web UI are 800 wide and always align, but a
  // hand-dropped file of any size may not.
  const int stride = draw->iWidth;
  const int valid = (draw->iWidthUsed > 0) ? draw->iWidthUsed : draw->iWidth;

  for (int row = 0; row < draw->iHeight; row++) {
    int y = draw->y + row + g_offsetY;
    if (y < 0 || y >= SCREEN_H) continue;

    const uint16_t *src = draw->pPixels + (size_t)row * stride;
    int xStart = draw->x + g_offsetX;

    // Clip horizontally rather than trusting the JPEG's dimensions.
    int from = 0;
    int count = valid;
    if (xStart < 0) {
      from = -xStart;
      count -= from;
      xStart = 0;
    }
    if (xStart + count > SCREEN_W) count = SCREEN_W - xStart;
    if (count <= 0) continue;

    memcpy(g_dest + (size_t)y * SCREEN_W + xStart, src + from,
           (size_t)count * sizeof(uint16_t));
  }
  return 1;
}

// Picks the largest JPEGDEC scale factor that still fits on the panel.
int chooseScale(int w, int h, int *outW, int *outH) {
  struct Option {
    int div;
    int flag;
  };
  const Option options[] = {{1, 0},
                            {2, JPEG_SCALE_HALF},
                            {4, JPEG_SCALE_QUARTER},
                            {8, JPEG_SCALE_EIGHTH}};

  for (const Option &o : options) {
    int sw = w / o.div;
    int sh = h / o.div;
    if (sw <= SCREEN_W && sh <= SCREEN_H) {
      *outW = sw;
      *outH = sh;
      return o.flag;
    }
  }
  *outW = w / 8;
  *outH = h / 8;
  return JPEG_SCALE_EIGHTH;
}

}  // namespace

bool decodeInto(const String &name, uint16_t *dest) {
  if (dest == nullptr || !storage::mounted()) return false;

  String safe = storage::sanitizeName(name);
  if (safe.length() == 0) return false;

  String path = String(DIR_PHOTOS) + "/" + safe;
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[photo] cannot open %s\n", path.c_str());
    return false;
  }

  size_t size = f.size();
  if (size == 0 || size > 4UL * 1024 * 1024) {
    f.close();
    Serial.printf("[photo] %s has an implausible size (%u bytes)\n",
                  safe.c_str(), (unsigned)size);
    return false;
  }

  // JPEGDEC's openRAM path is markedly faster than streaming from SD, and a
  // few hundred KB in PSRAM is cheap.
  uint8_t *raw = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (raw == nullptr) {
    f.close();
    Serial.println("[photo] out of PSRAM for the JPEG");
    return false;
  }
  size_t read = f.read(raw, size);
  f.close();

  if (read != size) {
    heap_caps_free(raw);
    Serial.printf("[photo] short read on %s\n", safe.c_str());
    return false;
  }

  bool ok = false;
  if (g_jpeg.openRAM(raw, (int)size, drawCallback)) {
    // We write straight into a uint16_t framebuffer that the RGB peripheral
    // scans out, so we want native byte order — not the big-endian layout that
    // SPI displays expect.
    g_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);

    int scaledW = 0;
    int scaledH = 0;
    int scale = chooseScale(g_jpeg.getWidth(), g_jpeg.getHeight(), &scaledW,
                            &scaledH);

    // Centre whatever we end up with, and clear first so letterbox bars are
    // black rather than the previous photo.
    memset(dest, 0, SCREEN_PIXELS * sizeof(uint16_t));
    g_dest = dest;
    g_offsetX = (int16_t)((SCREEN_W - scaledW) / 2);
    g_offsetY = (int16_t)((SCREEN_H - scaledH) / 2);

    uint32_t started = millis();
    ok = g_jpeg.decode(0, 0, scale) == 1;
    if (ok) {
      Serial.printf("[photo] %s %dx%d -> %dx%d in %lu ms\n", safe.c_str(),
                    g_jpeg.getWidth(), g_jpeg.getHeight(), scaledW, scaledH,
                    (unsigned long)(millis() - started));
    } else {
      Serial.printf("[photo] decode failed on %s\n", safe.c_str());
    }

    g_jpeg.close();
    g_dest = nullptr;
  } else {
    Serial.printf("[photo] %s is not a readable JPEG\n", safe.c_str());
  }

  heap_caps_free(raw);
  return ok;
}

}  // namespace photos

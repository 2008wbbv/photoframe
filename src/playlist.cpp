#include "playlist.h"

#include <esp_random.h>

#include "config.h"
#include "storage.h"

namespace playlist {
namespace {

String g_names[MAX_PHOTOS];
uint16_t g_order[MAX_PHOTOS];
uint16_t g_count = 0;
uint16_t g_pos = 0;

uint32_t randomBelow(uint32_t bound) {
  return bound == 0 ? 0 : esp_random() % bound;
}

// Shuffles g_order in place. If `avoidFirst` names a valid index and there is
// more than one photo, makes sure that index does not end up first — otherwise
// a reshuffle can show the same photo twice in a row.
void shuffle(int32_t avoidFirst) {
  for (uint16_t i = 0; i < g_count; i++) g_order[i] = i;

  for (uint16_t i = g_count; i > 1; i--) {
    uint16_t j = (uint16_t)randomBelow(i);
    uint16_t tmp = g_order[i - 1];
    g_order[i - 1] = g_order[j];
    g_order[j] = tmp;
  }

  if (g_count > 1 && avoidFirst >= 0 && g_order[0] == (uint16_t)avoidFirst) {
    uint16_t j = 1 + (uint16_t)randomBelow(g_count - 1);
    uint16_t tmp = g_order[0];
    g_order[0] = g_order[j];
    g_order[j] = tmp;
  }
}

}  // namespace

bool rescan() {
  // Remember what is on screen so a rescan (after an upload, say) does not
  // yank the current photo away.
  String showing = g_count > 0 ? g_names[g_order[g_pos]] : String();

  uint16_t n = 0;
  if (!storage::listPhotos(g_names, MAX_PHOTOS, &n)) {
    g_count = 0;
    g_pos = 0;
    return false;
  }
  g_count = n;
  g_pos = 0;
  if (g_count == 0) return true;

  shuffle(-1);

  // Re-point at the photo that was already showing, if it survived.
  if (showing.length() > 0) {
    for (uint16_t i = 0; i < g_count; i++) {
      if (g_names[g_order[i]] == showing) {
        g_pos = i;
        break;
      }
    }
  }

  Serial.printf("[playlist] %u photos\n", g_count);
  return true;
}

uint16_t count() { return g_count; }

bool empty() { return g_count == 0; }

String current() {
  if (g_count == 0) return String();
  return g_names[g_order[g_pos]];
}

void advance() {
  if (g_count == 0) return;
  if (g_pos + 1 < g_count) {
    g_pos++;
  } else {
    shuffle(g_order[g_pos]);  // finished a pass — new order, no repeat
    g_pos = 0;
  }
}

void back() {
  if (g_count == 0) return;
  g_pos = (g_pos == 0) ? (uint16_t)(g_count - 1) : (uint16_t)(g_pos - 1);
}

uint16_t position() { return g_pos; }

bool jumpTo(const String &name) {
  for (uint16_t i = 0; i < g_count; i++) {
    if (g_names[g_order[i]] == name) {
      g_pos = i;
      return true;
    }
  }
  return false;
}

String peekNext() {
  if (g_count == 0) return String();
  if (g_pos + 1 < g_count) return g_names[g_order[g_pos + 1]];
  // About to wrap and reshuffle, so the next photo is not knowable yet.
  return String();
}

}  // namespace playlist

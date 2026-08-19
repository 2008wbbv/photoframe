// Receives photos and messages sent to a Telegram bot.
//
// Nothing is "installed" on the ESP32 for this — Telegram exposes a plain HTTPS
// REST API, so the frame is only ever making web requests:
//
//   getUpdates   asks whether anything new has arrived
//   getFile      turns a photo's file_id into a download path
//   file/...     downloads the actual JPEG bytes
//   sendMessage  acknowledges back to the sender
//
// To set it up: message @BotFather on Telegram, send /newbot, and paste the
// token it gives you into config.h. Then message your own bot once and watch the
// serial console — it prints the chat id of anyone who talks to it, and only ids
// on the allowlist are accepted.
//
// The allowlist matters. A bot token is effectively public once it is on a device
// you have given away, and without it anyone who found the bot could put whatever
// they liked on the frame.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace telegram {

// True when a token is configured. No network needed to answer this.
bool configured();

void begin();

// Call from the main loop. Returns true if something arrived and was accepted,
// which is the caller's cue to refresh the playlist or show the message.
//
// This blocks for up to a few seconds while an HTTPS request is in flight, so it
// is only called when the frame has nothing more urgent to do.
bool poll();

// The most recent accepted text message, and when it landed. Empty if none.
String lastMessage();
String lastSender();
uint32_t lastMessageAt();

// Clears the pending message once it has been shown.
void clearMessage();

// True if a message arrived that has not been displayed yet.
bool messagePending();

}  // namespace telegram

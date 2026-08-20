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
// To set it up: message @BotFather on Telegram, send /newbot, and paste the token
// into the frame's setup page. Then message your own bot once — the page will
// show your chat id and offer to allow it.
//
// The token and allowlist live in NVS, not in config.h. A token compiled into a
// source file ends up in git history the first time the repo is pushed and stays
// there even if the line is later deleted; anyone who reads it controls the bot.
//
// The allowlist matters just as much. A token sitting inside a device you have
// given away is effectively public, and without an allowlist anyone who found the
// bot could put whatever they liked on the frame.

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

// --- setup, driven by the web page ---------------------------------------

// Stores the bot token. Pass an empty string to disable Telegram entirely.
// Returns false if the value does not look like a token at all.
bool setToken(const String &token);

bool hasAllowedSenders();
uint8_t allowedCount();

// The last sender that was turned away, so the page can offer to allow them
// rather than making anyone read a serial console. Zero when there is none.
int64_t pendingChatId();
String pendingName();
bool allowPending();

// True if a message arrived that has not been displayed yet.
bool messagePending();

}  // namespace telegram

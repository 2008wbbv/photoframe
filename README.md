# Rachel's Frame

A digital picture frame for the Waveshare ESP32-S3-Touch-LCD-4.3, with two
buttons, ambient-light dimming, a QR code that puts an upload page on your phone,
and a Telegram bot so you can send photos and notes to it from anywhere.

```
left  tap    previous photo
right tap    next photo
right hold   QR code → join the frame's Wi-Fi and add photos
left  hold   how long each photo stays up
```

Photos and notes texted to its Telegram bot appear on it within seconds, from
anywhere.

The touchscreen mirrors all four as a backup: the left half of the panel acts as
the left button, the right half as the right button, and holding is holding. So
every control stays reachable if a switch ever fails.

## Hardware

| | |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-4.3 — ESP32-S3-WROOM-1-N16R8, 800×480 RGB, 16 MB flash, 8 MB PSRAM |
| Light sensor | BH1750 on a GY-302 module, on the I2C terminal |
| Buttons | Two momentary switches sharing GPIO6, on the sensor terminal |
| Storage | microSD in the onboard slot, **FAT32** |

Both buttons land on one pin because that is all the board offers. Of the four
terminals on the end, only two carry GPIO at all — the 4-pin I2C terminal, which
the light sensor takes, and the 3-pin sensor terminal, which brings out GPIO6. The
RS485 and CAN terminals sit on the far side of their transceivers and carry
differential pairs, so GPIO15/16 and GPIO19/20 never reach a connector. The two
buttons therefore share GPIO6 through a 10k resistor ladder and are told apart by
voltage: 0 V for left, 1.65 V for right, 3.3 V idle.

**Read [`docs/WIRING.md`](docs/WIRING.md) before you solder.** There is one
non-obvious trap: the GY-302's default I2C address collides with the chip that
runs your backlight and SD card, and the symptom looks like a display fault
rather than a sensor fault. It is a one-wire fix.

[`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md) covers the rest — card prep, flashing,
a bench-test checklist to work through before anything gets mounted, where to put
the sensor, and what to tell whoever ends up using it.

## Build and flash

**Requires PlatformIO Core 6.1.19 or newer.** Check with `pio --version`. Older
Core refuses the platform below with `IncompatiblePlatform: ... depends on
PlatformIO Core >=6.1.19`, installs it, then rolls it straight back out again.

To upgrade, delete `~/.platformio/penv` and let the VS Code extension rebuild it
— see [`docs/DEPLOYMENT.md`](docs/DEPLOYMENT.md). Avoid `pio upgrade` on Windows;
it rewrites the venv it is running from and can leave Core unable to start.

```sh
pio run --target upload
pio device monitor
```

Nothing to upload to a filesystem — the web page is compiled into the firmware,
and photos live on the SD card.

The first build pulls a few hundred MB of toolchain, so give it a few minutes.

Verified building clean against Arduino core 3.3.9: **flash 1.24 MB of 6.55 MB
(18.8%), RAM 87 KB of 320 KB (26.4%)**, with the two 750 KB image buffers coming
out of PSRAM on top of that.

### A note on the platform line

`platformio.ini` points at the **pioarduino** fork rather than
`platformio/espressif32`. This is not incidental: Arduino_GFX 1.6 includes
`esp32-hal-periman.h` and `esp_private/periph_ctrl.h` unconditionally, and
neither exists in Arduino core 2.x. The official `platformio/espressif32` package
still ships core 2.0.17 even at version 7.0.1, so it cannot compile this. If you
change that line and the build dies on a missing `esp32-hal-periman.h`, that is
what happened.

## How it behaves

### The playlist

Photos are shuffled with a Fisher–Yates pass over `/photos`, and that order is
held for a full cycle. This matters for the back button: tapping left returns to
the photo you actually just saw, rather than jumping somewhere random. When a
pass finishes the deck is reshuffled, and the reshuffle explicitly avoids
starting with the photo that just ended the previous pass, so you never get the
same picture twice in a row.

The next photo is decoded into a spare PSRAM buffer while the current one is on
screen, so tapping right is instant instead of showing a blank panel for half a
second.

### Adding photos

Hold the right button and a QR code appears. On her Wi-Fi it encodes the frame's
address, so scanning it just opens the upload page — the phone is already on the
right network. Before setup, when the frame is hosting its own network, it encodes
a Wi-Fi join payload instead and the captive portal opens the page by itself; the
password is printed beside the code as well as baked into it, so nobody has to
type anything either way.

The browser does the image work. Each photo is cover-cropped and resized to
exactly 800×480 and re-encoded as JPEG before it is sent, along with a small
thumbnail for the management grid. Two useful consequences:

- **iPhone HEIC files just work.** Safari decodes them into a canvas; the frame
  never sees a format it cannot handle.
- **The frame stays fast.** It only writes bytes to the card. No server-side
  resizing, and no oversized JPEGs to struggle with at display time.

You can also drop JPEGs onto `/photos` on the card by hand. Those get
downscaled by the nearest power of two the decoder supports and centred, so they
may letterbox — the web UI's crop is a better fit for the panel. They will have
no thumbnail in the grid.

### Sending to it from anywhere

Text a photo or a note to a Telegram bot and it lands on the frame within
seconds, from any network in the world. Nothing is installed on the ESP32 for
this — Telegram is a plain HTTPS REST API, so the frame is only making web
requests to `api.telegram.org`:

| Call | Purpose |
|---|---|
| `getUpdates` | asks whether anything new has arrived |
| `getFile` | turns a photo's `file_id` into a download path |
| `file/…` | downloads the JPEG, streamed straight to the SD card |
| `sendMessage` | acknowledges back to the sender |

Setup is three steps: message **@BotFather**, send `/newbot`, paste the token into
`config.h`. Then message your own bot once — the serial console prints the chat id
of whoever talks to it, and you add that to the allowlist.

**The allowlist is not optional.** A bot token living inside a device you have
given away is effectively public, and without it anyone who found the bot could
put anything at all on someone's picture frame. Only ids in
`TELEGRAM_ALLOWED_CHAT_IDS` are accepted; everything else is logged and dropped.

Two honest notes. Polling is a short poll every five seconds rather than a
30-second long poll, because the main loop is single-threaded and parking it
inside an HTTPS read would freeze the slideshow, the buttons and the touchscreen
for the duration. And the TLS connection does not validate Telegram's certificate
chain: pinning a root CA is stronger, but Telegram rotates issuers and an expired
pin would silently kill a frame sitting on a shelf with no way to update it. The
exposure is that somebody already inside the network path could read the bot
token, and the allowlist limits what that buys them.

Photos arriving this way are **not** cropped to 800×480 the way web uploads are —
the phone is not doing the resizing, so they letterbox to fit. Telegram offers
each photo at several resolutions and the frame takes the largest one under
`TELEGRAM_MAX_PHOTO_WIDTH`.

### Her Wi-Fi, without you knowing the password

Reaching the internet means being on her network rather than hosting its own, so:

- **Credentials stored** → the frame joins that network and is reachable at
  `photoframe.local` or its address.
- **None stored, or the join fails** → it hosts `Rachel's Frame` and serves a
  setup page with a scanned list of nearby networks and a password field.

She types her own password, on her own phone, and it is stored in NVS. It never
goes into a source file or through whoever built the thing. If the network
disappears for more than five minutes the frame reopens the setup portal by
itself, so a new router does not turn it into a brick.

The QR code adapts to which of those is true. Hosting its own network, it is a
Wi-Fi join payload so scanning connects the phone. On her network, the phone is
already there too — so it is just the address, and scanning opens the page with no
network switching at all.

### Touch as a backup

The GT911 is polled on the same I2C bus and emits the *same events* the buttons
do, split down the middle of the panel — left half is the left button, right half
the right. The state machine never learns which input acted, so there is exactly
one set of behaviours to reason about rather than two that can drift apart.

Contacts shorter than 40 ms are discarded so a sleeve or a duster brushing the
glass does not skip a photo. Set `TOUCH_ENABLED` to `false` in `config.h` to
ignore the panel entirely.

### Cycle time

Hold the left button to open the interval picker, then tap either button to move
through `10s · 30s · 1m · 5m · 15m · 30m · 1h`. It saves and closes after five
seconds of no input, or immediately on another hold. The same setting is in the
web UI. It persists to `/config.txt` on the card, which is plain `key=value`
text you can edit directly.

### Auto-dimming, and why it works the way it does

The BH1750 is sampled five times a second, smoothed with an exponential moving
average, and mapped logarithmically to brightness — logarithmically because
perceived brightness is. Changes ease in rather than snapping, so someone walking
past the sensor does not make the frame flinch. Below about 1 lux the panel
blanks entirely; any button press wakes it for 30 seconds. The slideshow does not
advance while it is asleep.

**Brightness is applied in software, by scaling pixel values as each photo is
blitted into the framebuffer.** That is not a stylistic choice. On this board the
backlight is a plain digital on/off line behind the CH422G I2C expander — it is
not wired to a PWM-capable GPIO, so there is no backlight level to turn down.
Scaling the image is the only dimming available without modifying the hardware.

It works well in practice and costs nothing, but be aware of the honest
limitation: the backlight stays at full power, so in a properly dark room a
"dimmed" picture is still a faintly glowing rectangle rather than a truly dark
one. That is why night mode blanks the panel outright instead of just dimming
far down.

If you want real backlight PWM later, the mod is: cut the backlight enable trace
from CH422G `EXIO2`, drive the backlight EN pin from a spare GPIO, and attach it
to an LEDC channel. Then feed that channel from `display::setBrightness()` and
leave the pixel scaling at full. Everything else in the firmware stays as it is.

## Layout

```
src/
  main.cpp       state machine, dimming control, button handling
  config.h       every pin and tunable
  display.cpp    RGB panel, PSRAM buffers, software dimming, overlays
  photos.cpp     JPEG → RGB565 out of the SD card
  playlist.cpp   shuffle and navigation
  buttons.cpp    ADC ladder decode, debounce, short vs long press
  touch.cpp      GT911 driver, gestures mapped onto button events
  wifimgr.cpp    station/portal modes, credentials in NVS, network scan
  telegram.cpp   HTTPS polling, photo download, allowlist
  bh1750.cpp     light sensor
  ch422g.cpp     I/O expander: backlight, LCD reset, SD chip-select
  storage.cpp    SD mount, settings, filename hygiene
  webui.cpp      access point, captive portal, upload endpoints
  web_page.h     the upload page and gallery
docs/
  WIRING.md      connector map, the BH1750 address trap, button ladder
  DEPLOYMENT.md  card prep, flashing, bench test, mounting, handover
```

Two decisions worth knowing if you extend this:

**The synchronous `WebServer`, not `ESPAsyncWebServer.`** It runs on the main
task, so an upload writing to the SD card cannot race the slideshow reading from
it, and no locking is needed anywhere. The trade is that the slideshow pauses
during uploads — which is fine, since nobody is watching the frame while they are
adding to it.

**Two full-frame PSRAM buffers plus the panel's own framebuffer.** About 2.2 MB
of the 8 MB. The front buffer always holds the current photo at full brightness,
so re-dimming is a re-blit and never a re-decode.

## Things that will trip you up

| Symptom | Cause |
|---|---|
| Backlight dead or flickering, SD mounts intermittently | BH1750 left at 0x23, fighting the CH422G. See `docs/WIRING.md`. |
| `no BH1750 found` in the log | Wiring, or `ADDR` not actually tied to 3V3. Dimming disables itself and the frame runs at full brightness. |
| Card will not mount | exFAT. Reformat as FAT32. |
| Shimmer or torn rows | Drop `LCD_PCLK_HZ` in `config.h` to `14000000`. |
| Night mode never blanks | `EXIO2` vs `EXIO3` differ across board revisions; swap `EXIO_DISP` and `EXIO_LCD_RST` in `ch422g.h`. |
| Photos advance by themselves; menus open unprompted | GPIO6 is floating. The button ladder's 10k pull-up is missing. |
| Both buttons do the same thing | The right button's 10k series resistor is missing or bridged. |
| Photos letterboxed with black bars | Added by hand to the card, or sent via Telegram — neither path crops to 800×480 the way a web upload does. |
| Telegram messages ignored | Your chat id is not in `TELEGRAM_ALLOWED_CHAT_IDS`. The console prints the id of anyone who tries. |
| Frame stuck hosting its own network | It could not join the stored network. Connect to `Rachel's Frame` and the setup card will be waiting on the page. |

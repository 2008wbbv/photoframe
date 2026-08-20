# Deployment

Getting from a bare board to something you can hand over. Work through it in
order — the bench test in step 6 is the one to not skip, because half of these
problems are much easier to fix before the thing is glued into a frame.

## What you need

| | |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-4.3 |
| Sensor | GY-302 / BH1750 module |
| Buttons | 2 × momentary switches |
| Resistors | 2 × 10 kΩ, for the button ladder |
| Card | microSD, **FAT32**, 32 GB or smaller |
| Power | USB-C supply, 5 V, 1 A or better |
| Cable | USB-C data cable for flashing |

The board's own cable kit covers the connections: the 4-pin lead goes to the I2C
terminal for the light sensor, the 3-pin lead to the sensor/ADC terminal for the
buttons. Both buttons share that one 3-pin lead — see `WIRING.md` for why.

A 32 GB ceiling is not arbitrary: FAT32 is the only filesystem the ESP32's SD
driver mounts here, and Windows will not format larger cards as FAT32 without
third-party tools. exFAT will not work.

At ~150 KB per photo, even an 8 GB card holds far more than you will ever put on
it. Buy small.

## 1. Prepare the card

Format FAT32, then insert it into the onboard slot. That is all — the firmware
creates `/photos` and `/thumbs` on first boot.

If you want photos on it before you hand it over, you can either upload them
through the web UI later (better — they get cropped to fit) or drop JPEGs into a
`/photos` folder yourself now. Hand-dropped files are not cropped, so they will
letterbox with black bars unless they are already 800×480, and they will have no
gallery thumbnail until you re-upload them.

## 2. Wire it

Follow [`WIRING.md`](WIRING.md). Two things there are easy to get wrong and both
produce confusing symptoms:

- The BH1750's **`ADDR` pin must go to 3V3**. One wire, and getting it wrong
  looks like a broken display rather than a broken sensor.
- The buttons' **10k pull-up is required**. Without it the frame appears to press
  its own buttons.

Both buttons land on the single 3-pin sensor terminal through a resistor ladder,
because that terminal carries the only free GPIO the board brings out — the RS485
and CAN terminals are on the far side of their transceivers and carry differential
pairs, not pins you can wire a switch to.

## 3. Optional: the Telegram bot

Skip if you only want to add photos over Wi-Fi at home. Do it if you want to text
photos and notes to the frame from anywhere.

1. Message **@BotFather** and send `/newbot`. It hands you a token like
   `8123456:AAH9x...`.
2. Flash the frame, connect to it, and paste the token into the **Send from
   anywhere** card on its web page.
3. Message your own bot anything. Reload the page — it will show who messaged and
   offer **Allow them**. Tap it.

**Do not put the token in `config.h`.** It would go into git history the first
time you push and stay there even if you delete the line later, and anyone who
reads it controls your bot. The setup page stores it in NVS instead, which also
means changing it never needs a reflash.

If a token ever does leak, `/revoke` in BotFather issues a new one and instantly
kills the old.

**The allowlist is not optional either.** A token inside a device you have given
away is effectively public, so without it anyone who found the bot could put
anything they liked on her picture frame. Repeat step 3 for anyone else you want
to let send.

## 4. Set your Wi-Fi password before flashing

This is the password for the frame's *own* setup network, not hers. Open
`src/config.h` and change:

```c
static const char *const AP_PASSWORD = "photoframe";
```

Anyone in range who can read the QR off the screen can upload to the frame while
it is in setup mode, so this is worth a minute. At least 8 characters for WPA2.
Nobody has to type it — the QR carries it — so make it whatever you like.
`AP_SSID` is on the line above if you want a different name.

**Her own Wi-Fi password is not set here and you never need to know it.** On first
boot the frame hosts its network and serves a setup card with a list of nearby
networks; she picks hers, types her password on her own phone, and it is stored in
NVS. If you want to test on your own network first, do exactly the same thing —
then hand it over and she can redo it, or use the Forget button.

## 5. Flash

First check your PlatformIO Core version — this needs **6.1.19 or newer**:

```sh
pio --version
```

On older Core the build fails with `IncompatiblePlatform: Development platform
'espressif32' ... depends on PlatformIO Core >=6.1.19`. It installs the platform
and then removes it again, which looks alarming but breaks nothing.

### Upgrading Core

If you are behind, **delete the Python venv Core runs in and let the PlatformIO
VS Code extension rebuild it.** Restart VS Code afterwards:

```powershell
# Windows
Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\penv"
```

```sh
# macOS / Linux
rm -rf ~/.platformio/penv
```

This is safe: your downloaded toolchains and platforms live in
`~/.platformio/packages` and `~/.platformio/platforms`, not in `penv`, so nothing
large is re-fetched — only a small venv is rebuilt. Note that the bare `pio`
command may be missing from a normal terminal until the extension recreates it,
so use the terminal inside VS Code or the ✓ Build button.

**Avoid `pio upgrade` on a VS Code-managed install**, especially on Windows. It
tries to replace files inside the venv it is currently running from, and when
that half-completes you get a Core that cannot start at all:

```
ModuleNotFoundError: No module named 'click'
```

If you are already in that state, reinstall Core with its dependencies:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install --force-reinstall platformio
```

and if that still fails, delete `penv` as above.

Then:

```sh
pio run --target upload
pio device monitor
```

Do not use PlatformIO's **New Project** wizard. There is no board definition for
this Waveshare board — PlatformIO ships only three Waveshare ESP32-S3 definitions
and this is not one of them — so `platformio.ini` uses the generic
`esp32-s3-devkitc-1` and spells out the N16R8 module's real flash and PSRAM by
hand. Open the project folder and the config applies itself; there is no board to
select anywhere.

The first build downloads a few hundred MB of toolchain. Later builds take
seconds.

If the board is not detected, put it in download mode by hand: hold **BOOT**,
tap **RESET**, release **BOOT**, then upload again. After a successful flash,
tap **RESET** to run the firmware normally.

## 6. Bench test before you mount anything

**On first boot the frame shows the hardware check on its own screen** — every
subsystem with a live reading beside it, green for good and red for not. That is
the fastest way to catch a wiring mistake, because you can see the lux figure move
as you wave a hand over the sensor. Press right and it walks you through
calibrating the dimming: lights on, record, lights off, record.

Those two numbers become the ends of the brightness curve, which beats the generic
indoor defaults — a bright kitchen and a dim bedroom are not the same room. If the
two readings come out too close together it says so and lets you retry, rather
than saving a curve with no range in it. You can re-run the whole thing later from
the **Dimming** card on the web page.

The serial monitor shows the same story. You want to see all of these:

```
[buttons] resting at 3283 mV (idle should be above 2600)
[light] BH1750 found at 0x5C
[touch] GT911 found at 0x5D
[sd] mounted, ... MB total
[wifi] portal up: "Rachel's Frame" at http://192.168.4.1
[telegram] ready, 1 allowed sender(s)
[boot] ready — N photos, 30 sec per photo
```

Once a network is stored, the Wi-Fi line becomes
`[wifi] joined "HerNetwork" at http://192.168.1.42` followed by
`[wifi] also reachable at http://photoframe.local`.

Then check each thing by hand:

| Test | Expected |
|---|---|
| Tap left / right | Photo changes immediately |
| Hold right | QR code appears |
| Hold left, tap either button | Interval picker moves through the durations |
| Wait out the interval | Photo advances on its own |
| Cup your hand over the sensor | Picture dims within a second or two |
| Cover it completely | Panel blanks; a button press wakes it |
| Scan the QR with a phone | Joins the network, upload page opens by itself |
| Upload a photo | Appears in the gallery, and in the shuffle |
| Tap a gallery photo → Remove | Twice to confirm; it disappears |
| Tap the left / right half of the screen | Same as the matching button |
| Press and hold the right half | QR code appears, fully on screen and uncropped |
| Press and hold the left half | Interval picker appears |
| First boot | Hardware check appears before the slideshow |
| Walk the calibration | Lights on, right, lights off, right — lux moves as you do it |
| Join `Rachel's Frame`, pick her network, Connect | Frame restarts and joins it |
| Hold right again once joined | QR now carries the LAN address, not a join code |
| Text your bot a photo | Lands in the shuffle within a few seconds |
| Text your bot a note | Shows as a card for 30 s, then back to the slideshow |

Things worth knowing while you test:

- **`[light] *** BH1750 responded on 0x23 ***`** means the `ADDR` jumper did not
  take. Fix it now, not later.
- **Photos changing on their own, or a menu opening unprompted**, means GPIO6 is
  floating — check the 10k pull-up. The resting voltage in the boot log tells you
  directly: it should sit near 3300 mV, not wander.
- **Both buttons doing the same thing** means the right button's 10k series
  resistor is missing or bridged, so both are pulling the pin to 0 V.
- **`[touch] no GT911`** means the controller did not answer on either 0x5D or
  0x14. The buttons carry on working; only the touch backup is lost.
- **No `[light]` line at all** means the sensor is not on the bus. The frame
  still runs, just at full brightness with no dimming.
- **A blank or scrambled panel** usually means `LCD_PCLK_HZ` is too high for
  your unit. Drop it to `14000000` in `config.h`.
- **Night mode not blanking** means `EXIO2`/`EXIO3` are swapped on your board
  revision. Swap `EXIO_DISP` and `EXIO_LCD_RST` in `ch422g.h`.

## 7. Mount it

**Point the sensor at the room, not at the panel.** This is the one mounting
mistake that matters. If the screen's own light reaches the sensor you get a
feedback loop — bright screen reads as a bright room, so it stays bright, and it
never dims properly. Front-facing, near an edge of the frame, works well.

The rest:

- Buttons somewhere reachable but not somewhere a hand lands when picking the
  frame up — a stray long-press on the left button opens the interval menu.
- The panel is 800×480 in a 5:3 ratio. Cut the mat opening to match, or you will
  crop the photos twice.
- Strain-relieve the USB cable. It is the only thing holding power, and the port
  is surface-mounted.
- Leave some airflow behind the board. It runs warm continuously — the panel and
  backlight are on whenever the room is lit.

## 8. Power

Any decent 5 V USB-C supply rated 1 A or more. This runs continuously, so use a
real wall adapter rather than a spare phone charger of unknown provenance, and
do not power it from a laptop port you intend to unplug.

There is no battery and no shutdown sequence. Pulling power is safe — the only
writes are during uploads and when saving a setting, both of which are brief.

## 9. Handing it over

Everything she needs to know:

> - **Tap the buttons** to move back and forward through the photos.
> - **Hold the right button** for a QR code. Scan it with your phone camera and
>   the upload page opens — pick photos and they show up on the frame.
> - **Hold the left button** to change how long each photo stays up, then tap
>   either button to pick a time.
> - It dims itself as the room gets darker, and goes dark at night. Press any
>   button to wake it.
> - To delete one: pull up the upload page, tap the photo in the gallery, then
>   Remove.

That is the whole interface. There is nothing to plug in, no app, and no account.

## Later on

**Adding or removing photos** is the QR-code flow above — no computer needed,
ever.

**Backing up the photos**: power off, pull the card, copy `/photos`. Worth doing
once she has put things on it that only exist there.

**Reflashing** does not touch the card, so photos and settings survive a firmware
update. Settings live in `/config.txt` as plain `key=value` text you can edit in
any editor if you pull the card.

**Starting over on settings**: delete `/config.txt`. It gets rebuilt with the
defaults from `config.h` on the next save.

**If it stops responding**, pull power and plug it back in. If it comes up with
`[sd] mount failed`, reseat the card; the contacts on the onboard slot are not
generous.

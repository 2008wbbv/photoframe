# Deployment

Getting from a bare board to something you can hand over. Work through it in
order — the bench test in step 5 is the one to not skip, because half of these
problems are much easier to fix before the thing is glued into a frame.

## What you need

| | |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-4.3 |
| Sensor | GY-302 / BH1750 module |
| Buttons | 2 × momentary switches |
| Card | microSD, **FAT32**, 32 GB or smaller |
| Power | USB-C supply, 5 V, 1 A or better |
| Cable | USB-C data cable for flashing |

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

Follow [`WIRING.md`](WIRING.md). Do not skip the note about the BH1750's `ADDR`
pin — it is one wire, and getting it wrong produces symptoms that look like a
broken display rather than a broken sensor.

## 3. Set your password before flashing

Open `src/config.h` and change:

```c
static const char *const AP_PASSWORD = "photoframe";
```

Anyone within Wi-Fi range who can read the QR code off the screen can upload to
the frame, so this is worth a minute. It must be at least 8 characters for WPA2.
Nobody has to type it — the QR carries it — so make it whatever you like.

While you are in there, `AP_SSID` is on the line above if you want a different
network name.

## 4. Flash

```sh
pio run --target upload
pio device monitor
```

The first build downloads a few hundred MB of toolchain. Later builds take
seconds.

If the board is not detected, put it in download mode by hand: hold **BOOT**,
tap **RESET**, release **BOOT**, then upload again. After a successful flash,
tap **RESET** to run the firmware normally.

## 5. Bench test before you mount anything

Watch the serial monitor through a boot. You want to see all four of these:

```
[light] BH1750 found at 0x5C
[sd] mounted, ... MB total
[wifi] "Rachel's Frame" up at http://192.168.4.1
[boot] ready — N photos, 30 sec per photo
```

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

Things worth knowing while you test:

- **`[light] *** BH1750 responded on 0x23 ***`** means the `ADDR` jumper did not
  take. Fix it now, not later.
- **No `[light]` line at all** means the sensor is not on the bus. The frame
  still runs, just at full brightness with no dimming.
- **A blank or scrambled panel** usually means `LCD_PCLK_HZ` is too high for
  your unit. Drop it to `14000000` in `config.h`.
- **Night mode not blanking** means `EXIO2`/`EXIO3` are swapped on your board
  revision. Swap `EXIO_DISP` and `EXIO_LCD_RST` in `ch422g.h`.

## 6. Mount it

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

## 7. Power

Any decent 5 V USB-C supply rated 1 A or more. This runs continuously, so use a
real wall adapter rather than a spare phone charger of unknown provenance, and
do not power it from a laptop port you intend to unplug.

There is no battery and no shutdown sequence. Pulling power is safe — the only
writes are during uploads and when saving a setting, both of which are brief.

## 8. Handing it over

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

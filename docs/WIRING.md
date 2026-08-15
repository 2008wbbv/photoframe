# Wiring

Two things to solder: the light sensor and the buttons. Neither needs a resistor.

## The one thing that will bite you: the BH1750's address

**Solder the GY-302's `ADDR` pad to `3V3`.** Not GND, not floating.

The GY-302 ships with `ADDR` pulled low, which puts the BH1750 at I2C address
**0x23**. On this board 0x23 is already taken — by the CH422G I/O expander that
controls your backlight, the LCD reset line, and the SD card's chip-select.

The CH422G is unusual: it has no register pointer, so **each of its internal
registers is a separate I2C device address**. It answers on **0x23, 0x24, 0x26
and 0x38** all at once. Waveshare's own docs put it plainly — *"the I²C address
is not configurable as the CH422G has a separate address for each internal
register."*

Leave `ADDR` low and the light sensor and the expander will corrupt each other's
traffic on a bus you cannot avoid sharing. The failure is nasty because it does
not look like a sensor problem: you get a dead or flickering backlight, an SD
card that mounts intermittently, or a panel that will not come out of reset.

Tying `ADDR` high moves the sensor to **0x5C**, which is clear. The GT911 touch
controller sits on 0x5D / 0x14, so there is no conflict there either.

The firmware probes 0x5C first. If it finds a sensor on 0x23 instead it still
works, but prints a loud warning on the serial console — so if you are unsure
whether the jumper took, watch the boot log.

### Bus map after wiring

| Address | Device |
|---|---|
| 0x23, 0x24, 0x26, 0x38 | CH422G I/O expander (backlight, LCD reset, touch reset, SD CS) |
| 0x5D (or 0x14) | GT911 capacitive touch |
| **0x5C** | **BH1750, with ADDR → 3V3** |
| ~~0x23~~ | ~~BH1750 default — do not use~~ |

## BH1750 → the board's I2C header

The board breaks I2C out on a PH2.0 connector, which saves you tapping the
module pads directly.

| GY-302 | Board | GPIO |
|---|---|---|
| `VCC` | 3V3 | — |
| `GND` | GND | — |
| `SDA` | SDA | 8 |
| `SCL` | SCL | 9 |
| `ADDR` | **3V3** | — |

`ADDR` has no pin on the I2C connector — run a short wire from the module's
`ADDR` pad to its own `VCC` pad.

Mount the sensor so it faces the room, not the panel. Its own backlight leaking
onto it creates a feedback loop: bright screen → sensor reads bright → stays
bright. Facing forward next to the frame's edge works well.

## Buttons

Plain momentary switches, one leg to the GPIO and the other to GND. The
firmware enables the ESP32's internal pull-ups, so there is nothing else to add.

| Button | GPIO | Where |
|---|---|---|
| Left | 6 | ADC PH2.0 header |
| Right | 15 | CAN / RS485 PH2.0 header (TX side) |

### Why GPIO15 and not GPIO16

Both are on the CAN/RS485 connector, but they are not equally safe:

- **GPIO15** is `CANTX` / RS485 `TXD`. On the board it only feeds a transceiver
  *input*, which is high-impedance. Nothing fights your switch. Use this one.
- **GPIO16** is `CANRX` / RS485 `RXD` — a transceiver *output* that idles high.
  It would actively drive the pin against your button, so the press may not read
  reliably and you would be sinking the transceiver's drive current through the
  switch.

Confirm the GPIO numbering against your board's silkscreen or schematic before
soldering — Waveshare has shipped several revisions of the 4.3" boards, and the
`B` variant differs. If yours is laid out differently, both pins are single
constants at the top of `src/config.h`.

If GPIO6 turns out to be unavailable on your unit, GPIO44 (UART0 RXD) is the
next-best spare, since `Serial` runs over the S3's native USB here and leaves
UART0 idle.

## GPIO budget, for reference

The 800×480 RGB panel is why there is so little left over.

| Function | GPIO |
|---|---|
| RGB data | 1, 2, 42, 41, 40 (R) · 39, 0, 45, 48, 47, 21 (G) · 14, 38, 18, 17, 10 (B) |
| RGB control | 5 (DE), 3 (VSYNC), 46 (HSYNC), 7 (PCLK) |
| I2C | 8 (SDA), 9 (SCL) |
| Touch interrupt | 4 |
| SD card | 11 (MOSI), 12 (SCK), 13 (MISO) — CS is on CH422G EXIO4 |
| Flash + PSRAM | 26–37 — never touch these |
| Native USB | 19, 20 |
| UART0 | 43, 44 — free here; 43 is used as the SD library's dummy CS |
| **Buttons** | **6, 15** |

## The SD card's chip-select

Worth knowing if you go poking at the SD code: the card's CS is not a GPIO, it
is CH422G `EXIO4`. The Arduino `SD` library insists on driving a real pin, so
the firmware asserts `EXIO4` once at startup and leaves it asserted, then hands
the library GPIO43 to toggle instead. The card is the only device on that SPI
bus, so nothing is confused by CS never being released.

Format the card **FAT32**. exFAT will not mount.

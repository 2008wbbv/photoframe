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

## What the connectors on the end actually give you

This is the part that decides how the buttons get wired, and it is not what you
would guess from the pin table.

| Connector | Pins | What is on it |
|---|---|---|
| Sensor / ADC | 3P | `3V3`, `GND`, **GPIO6** — the only free GPIO on any connector |
| I2C | 4P | `3V3`, `GND`, `GPIO8` (SDA), `GPIO9` (SCL) |
| RS485 | 2P | Differential **A** / **B** — *not* GPIO |
| CAN | 2P | Differential **CANH** / **CANL** — *not* GPIO |

The RS485 and CAN terminals sit on the far side of their transceivers: GPIO15/16
stop at the SP3485 and GPIO19/20 stop at the TJA1051 (and those two are muxed
against native USB by CH422G `EXIO5`). The 120 Ω termination jumpers next to them
are the giveaway — termination only exists on a differential bus. **Neither
connector brings out a pin you can hang a switch on.**

So the board gives you exactly **one** usable GPIO without soldering to the
module: GPIO6, on the 3-pin sensor terminal. The I2C terminal is spoken for by
the light sensor.

## Buttons: two switches on one pin

Two buttons, one GPIO, so they are told apart by voltage instead of by pin. One
10k pull-up and one 10k series resistor is the whole circuit.

```
                   ┌──[10k]──── 3V3          (from the sensor connector)
                   │
  GPIO6 ───────────┤
                   │
                   ├──[ LEFT switch ]─────── GND
                   │
                   └──[10k]──[ RIGHT ]────── GND
```

Which gives three clearly separated levels:

| State | Voltage on GPIO6 | Reads as |
|---|---|---|
| Nothing pressed | 3.30 V | idle |
| Right pressed | 1.65 V | next photo |
| Left pressed | 0.00 V | previous photo |

A clean split into thirds, so the bands in `src/config.h` have ~500 mV of margin
on each side. Anything landing between bands is treated as no press, which means
a switch that is not properly closed does nothing rather than something
surprising. Press both and left wins, since a dead short beats the divider.

**The 10k pull-up is required, not optional.** Without it GPIO6 floats, drifts
through the bands, and the frame will appear to press its own buttons. If you see
phantom presses, that resistor is the first thing to check.

Any value from 4.7k to 22k works as long as **both resistors are the same
value** — the divider only needs to land near half rail, and equal resistors do
that regardless of the value you pick.

### Checking it

The boot log prints the resting voltage:

```
[buttons] resting at 3283 mV (idle should be above 2600)
```

Hold each button and watch the value: left should drop near 0, right to roughly
1650. If right reads about the same as left, the series resistor is shorted or
missing. If idle reads low or wanders, the pull-up is not connected.

Confirm GPIO6 is the sensor-terminal pin on your unit before soldering —
Waveshare has shipped several revisions of the 4.3" boards and the `B` variant
differs. If yours is wired differently, `PIN_BTN_ADC` is a single constant in
`src/config.h`. Should you ever need a second real GPIO, GPIO43/44 (UART0) are
free because `Serial` runs over native USB here, but they mean soldering to the
board rather than plugging into a connector.

## Touchscreen

Nothing to wire — the GT911 is already on the board, on the same I2C bus as
everything else (0x5D or 0x14; the firmware probes both). It acts as a backup for
the buttons: left half of the panel is the left button, right half is the right,
and holding is holding.

If touches land on the wrong half, the panel's GT911 config reports a different
orientation than assumed. The console prints every touch-down:

```
[touch] down at 634,210 (right half)
```

Compare that against where you actually pressed, then set `TOUCH_SWAP_XY` or
`TOUCH_INVERT_X` in `src/config.h`. `TOUCH_ENABLED = false` turns it off entirely.

## GPIO budget, for reference

The 800×480 RGB panel is why there is so little left over.

| Function | GPIO | On a connector? |
|---|---|---|
| RGB data | 1, 2, 42, 41, 40 (R) · 39, 0, 45, 48, 47, 21 (G) · 14, 38, 18, 17, 10 (B) | no |
| RGB control | 5 (DE), 3 (VSYNC), 46 (HSYNC), 7 (PCLK) | no |
| I2C | 8 (SDA), 9 (SCL) | **yes** — 4P terminal |
| Touch interrupt | 4 | no |
| SD card | 11 (MOSI), 12 (SCK), 13 (MISO) — CS is on CH422G EXIO4 | no |
| RS485 | 15 (TXD), 16 (RXD) | no — stops at the SP3485 |
| CAN | 19, 20 — shared with native USB via EXIO5 | no — stops at the TJA1051 |
| Flash + PSRAM | 26–37 — never touch these | no |
| UART0 | 43, 44 — free here; 43 is the SD library's dummy CS | no |
| **Buttons** | **6** (both, via the ladder above) | **yes** — 3P sensor terminal |

## The SD card's chip-select

Worth knowing if you go poking at the SD code: the card's CS is not a GPIO, it
is CH422G `EXIO4`. The Arduino `SD` library insists on driving a real pin, so
the firmware asserts `EXIO4` once at startup and leaves it asserted, then hands
the library GPIO43 to toggle instead. The card is the only device on that SPI
bus, so nothing is confused by CS never being released.

Format the card **FAT32**. exFAT will not mount.

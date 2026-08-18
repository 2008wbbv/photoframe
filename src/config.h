// Hardware + behaviour configuration for Rachel's Frame.
//
// Everything you might reasonably want to change lives in this one file.

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

static const int16_t SCREEN_W = 800;
static const int16_t SCREEN_H = 480;
static const uint32_t SCREEN_PIXELS = (uint32_t)SCREEN_W * SCREEN_H;

// RGB565 parallel bus. These are fixed by the board's traces — do not change
// them unless you are on a different Waveshare model.
static const int8_t PIN_LCD_DE = 5;
static const int8_t PIN_LCD_VSYNC = 3;
static const int8_t PIN_LCD_HSYNC = 46;
static const int8_t PIN_LCD_PCLK = 7;

// Listed least-significant bit first, which is how Arduino_GFX wants them.
static const int8_t PIN_LCD_R[5] = {1, 2, 42, 41, 40};
static const int8_t PIN_LCD_G[6] = {39, 0, 45, 48, 47, 21};
static const int8_t PIN_LCD_B[5] = {14, 38, 18, 17, 10};

// Drop to 14000000 if you see shimmer or torn rows on your unit.
static const uint32_t LCD_PCLK_HZ = 16000000;

// ---------------------------------------------------------------------------
// I2C — shared by the CH422G expander, the GT911 touch controller and BH1750
// ---------------------------------------------------------------------------

static const int8_t PIN_I2C_SDA = 8;
static const int8_t PIN_I2C_SCL = 9;
static const uint32_t I2C_HZ = 400000;

// ---------------------------------------------------------------------------
// BH1750 ambient light sensor (GY-302 module)
// ---------------------------------------------------------------------------
//
// IMPORTANT: the GY-302 ships with ADDR pulled low, which puts the sensor at
// 0x23 — and 0x23 is one of the four addresses the CH422G expander answers to
// (it uses a separate I2C address per internal register: 0x23, 0x24, 0x26,
// 0x38). Leaving ADDR low makes the light sensor fight the chip that drives
// your backlight, LCD reset and SD chip-select.
//
// Solder ADDR to 3V3 so the sensor lands on 0x5C instead. 0x5C is clear: the
// GT911 touch controller sits on 0x5D/0x14. See docs/WIRING.md.
static const uint8_t BH1750_ADDR_HIGH = 0x5C;  // ADDR -> 3V3  (use this one)
static const uint8_t BH1750_ADDR_LOW = 0x23;   // ADDR -> GND  (conflicts!)

// ---------------------------------------------------------------------------
// Buttons — both on one ADC pin, told apart by voltage
// ---------------------------------------------------------------------------
//
// Only ONE general-purpose GPIO reaches a connector on this board: GPIO6, on
// the 3-pin sensor/ADC terminal. Everything else is either consumed by the RGB
// bus or stops at a transceiver — the RS485 terminal carries differential A/B
// (GPIO15/16 end at the SP3485) and the CAN terminal carries CANH/CANL
// (GPIO19/20, muxed against native USB by CH422G EXIO5). Neither brings out a
// usable GPIO, so the alternative would be soldering to the module itself.
//
// Both buttons therefore share GPIO6 through a resistor ladder, and are told
// apart by the voltage they produce:
//
//   3V3 ──[10k]──┬────────────────────── GPIO6      idle    3.30 V
//                ├──[ LEFT  switch ]──── GND        left    0.00 V
//                └──[10k]──[ RIGHT ]──── GND        right   1.65 V
//
// The external 10k pull-up is required, not optional: without it GPIO6 floats
// and the frame sees phantom presses. See docs/WIRING.md.
static const int8_t PIN_BTN_ADC = 6;

// Bands in millivolts, read through the ADC's factory calibration so they do not
// depend on the attenuation setting. Nominal readings are 0 / 1650 / 3300, a
// clean split into thirds. Anything landing between bands counts as no press, so
// a finger half off a switch cannot trigger something unintended.
static const uint32_t BTN_MV_LEFT_MAX = 700;
static const uint32_t BTN_MV_RIGHT_MIN = 1150;
static const uint32_t BTN_MV_RIGHT_MAX = 2150;
static const uint32_t BTN_MV_IDLE_MIN = 2600;

// The ADC is noisy enough that a few samples are worth averaging.
static const uint8_t BTN_ADC_SAMPLES = 4;

static const uint32_t BTN_DEBOUNCE_MS = 25;
static const uint32_t BTN_LONG_PRESS_MS = 700;

// ---------------------------------------------------------------------------
// Touchscreen — a backup for the buttons
// ---------------------------------------------------------------------------
//
// The GT911 shares the I2C bus above and emits the same events the buttons do:
// the left half of the screen acts as the left button, the right half as the
// right button, and holding is holding. Every control is therefore reachable by
// finger if a switch ever fails.
//
// Set this to false to make the panel ignore touch entirely.
static const bool TOUCH_ENABLED = true;

// The controller only produces a new frame every ~10 ms, so polling faster just
// burns I2C bandwidth that the light sensor also wants.
static const uint32_t TOUCH_POLL_INTERVAL_MS = 20;

// Contacts shorter than this are discarded. Guards against a sleeve or a duster
// brushing the glass and skipping a photo.
static const uint32_t TOUCH_MIN_TAP_MS = 40;

// The GT911's own config decides what orientation it reports in, and that is
// baked into the panel rather than something we set. If the boot log shows
// touches landing on the wrong half, or x and y transposed, flip these. The
// console prints every touch-down coordinate, so it is quick to tell.
static const bool TOUCH_SWAP_XY = false;
static const bool TOUCH_INVERT_X = false;

// Safety valve. Nobody holds a finger down for this long, so if we still believe
// one is down after this the controller has gone quiet mid-contact — and a stuck
// "held" would pin the interval menu open forever on a device meant to run for
// months unattended.
static const uint32_t TOUCH_MAX_HOLD_MS = 10UL * 1000;

// ---------------------------------------------------------------------------
// SD card (SPI). Chip-select is NOT a GPIO — it hangs off CH422G EXIO4.
// ---------------------------------------------------------------------------

static const int8_t PIN_SD_MOSI = 11;
static const int8_t PIN_SD_SCK = 12;
static const int8_t PIN_SD_MISO = 13;

// The Arduino SD library insists on toggling a real CS pin. The card is the
// only device on this bus, so we hold the true CS (EXIO4) asserted forever and
// hand the library a spare pin to wiggle harmlessly.
//
// GPIO43 is UART0 TXD, which nothing uses here — Serial goes over the S3's
// native USB (ARDUINO_USB_CDC_ON_BOOT), so this pin is idle after boot.
static const int8_t PIN_SD_DUMMY_CS = 43;

static const uint32_t SD_SPI_HZ = 20000000;

static const char *const DIR_PHOTOS = "/photos";
static const char *const DIR_THUMBS = "/thumbs";
static const char *const PATH_CONFIG = "/config.txt";

static const uint16_t MAX_PHOTOS = 500;

// ---------------------------------------------------------------------------
// Wi-Fi access point
// ---------------------------------------------------------------------------

static const char *const AP_SSID = "Rachel's Frame";
// WPA2 needs at least 8 characters. Change this if you like — the QR code on
// screen carries it, so nobody ever has to type it.
static const char *const AP_PASSWORD = "photoframe";
static const uint8_t AP_CHANNEL = 6;
static const uint8_t AP_MAX_CLIENTS = 4;

// ---------------------------------------------------------------------------
// Slideshow
// ---------------------------------------------------------------------------

// Options offered by the hold-left interval picker.
static const uint32_t INTERVAL_OPTIONS_MS[] = {
    10UL * 1000,        // 10 seconds
    30UL * 1000,        // 30 seconds
    60UL * 1000,        // 1 minute
    5UL * 60 * 1000,    // 5 minutes
    15UL * 60 * 1000,   // 15 minutes
    30UL * 60 * 1000,   // 30 minutes
    60UL * 60 * 1000,   // 1 hour
};
static const uint8_t INTERVAL_OPTION_COUNT =
    sizeof(INTERVAL_OPTIONS_MS) / sizeof(INTERVAL_OPTIONS_MS[0]);
static const uint8_t INTERVAL_DEFAULT_INDEX = 1;  // 30 seconds

static const uint32_t QR_OVERLAY_TIMEOUT_MS = 60UL * 1000;
static const uint32_t MENU_IDLE_TIMEOUT_MS = 5UL * 1000;

// ---------------------------------------------------------------------------
// Auto-dimming
// ---------------------------------------------------------------------------
//
// The backlight on this board is a plain on/off line behind the I2C expander,
// not a PWM-capable GPIO, so brightness is applied by scaling pixel values as
// we blit into the framebuffer. Below NIGHT_LUX we blank the panel outright.
// See README for the optional hardware mod that gets you true backlight PWM.

static const uint8_t BRIGHTNESS_MIN_DEFAULT = 40;   // never darker than this
static const uint8_t BRIGHTNESS_MAX_DEFAULT = 255;  // full brightness

// Lux at which we reach min / max brightness. Mapped logarithmically between.
static const float LUX_AT_MIN_BRIGHTNESS = 2.0f;
static const float LUX_AT_MAX_BRIGHTNESS = 400.0f;

// Below this, turn the panel off entirely. A button press wakes it.
static const float NIGHT_LUX_DEFAULT = 1.0f;
static const uint32_t NIGHT_WAKE_MS = 30UL * 1000;

static const uint32_t LUX_SAMPLE_INTERVAL_MS = 200;
static const float LUX_SMOOTHING = 0.15f;  // EMA weight on each new sample

// How fast brightness is allowed to move, so clouds and passing shadows fade
// rather than snap.
static const uint32_t BRIGHTNESS_SLEW_INTERVAL_MS = 50;
static const uint8_t BRIGHTNESS_SLEW_STEP = 4;

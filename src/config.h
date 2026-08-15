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
// Buttons — wired switch-to-GND, using the ESP32's internal pull-ups
// ---------------------------------------------------------------------------
//
// Almost every GPIO on this board is consumed by the RGB bus. These two are
// broken out on PH2.0 connectors and are free as long as you are not using
// the ADC / CAN / RS485 peripherals.
//
// GPIO15 rather than GPIO16 for the right button, deliberately: GPIO15 is
// CANTX / RS485 TXD, so onboard it only ever feeds a transceiver *input*, which
// is high-impedance and will not fight the button. GPIO16 is CANRX / RS485 RXD
// — a transceiver *output* that idles high and would drive against the switch.
static const int8_t PIN_BTN_LEFT = 6;   // ADC header
static const int8_t PIN_BTN_RIGHT = 15; // CAN/RS485 header, TX side

static const uint32_t BTN_DEBOUNCE_MS = 25;
static const uint32_t BTN_LONG_PRESS_MS = 700;

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

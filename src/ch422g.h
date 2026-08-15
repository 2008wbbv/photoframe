// CH422G I/O expander — drives LCD reset, display enable, touch reset and the
// SD card's chip-select on this board.
//
// The CH422G is unusual: it has no register pointer. Each internal register is
// a separate I2C device address, and you write a single raw byte to it. That is
// why it squats on four addresses at once (0x23, 0x24, 0x26, 0x38) and why the
// BH1750 must not be left at its default 0x23.

#pragma once

#include <stdint.h>

namespace ch422g {

// Bit positions within the IO0..IO7 output byte.
static const uint8_t EXIO_TP_RST = 1;  // touch controller reset
static const uint8_t EXIO_DISP = 2;    // display enable / backlight
static const uint8_t EXIO_LCD_RST = 3; // LCD reset
static const uint8_t EXIO_SD_CS = 4;   // SD card chip-select (active low)
static const uint8_t EXIO_USB_SEL = 5; // USB / CAN mux select

// Puts IO0..IO7 into push-pull output mode and applies the boot state:
// touch and LCD released from reset, display enabled, SD chip-select asserted.
// Returns false if the expander did not acknowledge on the bus.
bool begin();

// Sets one EXIO line. Changes are written to the expander immediately.
void write(uint8_t bit, bool high);

// Display enable. Because this is a digital line, false blanks the panel
// completely — there is no dimming available through it.
void setDisplayEnabled(bool on);

// Pulses the LCD and touch reset lines low, then releases them.
void resetPanel();

}  // namespace ch422g

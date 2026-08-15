// BH1750FVI ambient light sensor, continuous high-resolution mode.
//
// Small enough not to warrant a library dependency: three command bytes and a
// 16-bit read.

#pragma once

#include <stdint.h>

namespace bh1750 {

// Probes 0x5C first, then 0x23. Returns false if neither responds.
//
// If the sensor is found on 0x23 this logs a loud warning: that address belongs
// to the CH422G expander and the two devices will corrupt each other's traffic.
// Strap the GY-302's ADDR pin to 3V3 to move it to 0x5C.
bool begin();

// True once begin() has found a sensor.
bool present();

// The address the sensor was actually found on.
uint8_t address();

// Reads illuminance. Returns a negative value if the read failed.
// The sensor integrates over ~120 ms, so polling faster than that just returns
// the same conversion again.
float readLux();

}  // namespace bh1750

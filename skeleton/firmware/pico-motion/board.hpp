#pragma once
// Pin map of the breadboard (guide 2.2, step 3). Channel n = servo_pins[n].

#include <array>
#include <cstdint>

namespace pico_motion {

// ch0 head pan, ch1 head tilt, ch2–7 hand (Phase 3). GP10–15: SPI1, reserved for the eyes (2.4) ⚠️; the arm
// (Phase 4) gets free pins once 2.4 decides where the display goes.
// GP2–9 = PWM slices 1–4, A/B each: both channels of a slice share its 50 Hz period, which is fine for servos.
inline constexpr std::array<uint8_t, 8> servo_pins = {2, 3, 4, 5, 6, 7, 8, 9};

} // namespace pico_motion

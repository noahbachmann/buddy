#pragma once
// Pin map of the breadboard (docs/motion-controller/pico.md, step 3). Channel n = servo_pins[n].

#include <array>
#include <cstdint>

namespace pico_motion {

// ch0 demo servo, ch1–7 free. GP10–15: SPI1, kept free for a display.
// GP2–9 = PWM slices 1–4, A/B each: both channels of a slice share its 50 Hz period, which is fine for servos.
inline constexpr std::array<uint8_t, 8> servo_pins = {2, 3, 4, 5, 6, 7, 8, 9};

} // namespace pico_motion

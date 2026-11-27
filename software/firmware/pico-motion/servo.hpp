#pragma once
// Servo channels on the RP2350's hardware PWM: 50 Hz, 1 µs per tick, so a channel's compare level *is* its pulse
// width in µs (0 = no pulse: the servo goes limp). Limits, clamping and linear ramps; no I/O besides the PWM pins.

#include "board.hpp"

#include <cstdint>

namespace pico_motion {

class Servos {
public:
    static constexpr int count = static_cast<int>(servo_pins.size());

    // All channels without pulses and without limits: a fresh Pico moves nothing.
    void init();

    static bool exists(int ch) { return ch >= 0 && ch < count; }
    bool limited(int ch) const { return channels_[ch].max_us != 0; }
    bool any_active() const;

    // Keeps driving if the current pulse is inside the new limits, else relaxes (no surprise jump).
    void limit(int ch, uint16_t min_us, uint16_t max_us);
    // Ramp from the current pulse to the clamped target over ms. From relaxed: jumps (the servo's position is
    // unknown). Returns the clamped target. update() reports DONE for every move, also for ms = 0.
    uint16_t move(int ch, uint16_t us, uint16_t ms, uint32_t now_ms);
    void relax(int ch);
    void relax_all();

    // Every 20 ms (one PWM period). done(ch, us) for each ramp that reached its target.
    template <typename F>
    void update(uint32_t now_ms, F&& done) {
        for (int ch = 0; ch < count; ++ch) {
            auto& c = channels_[ch];
            if (!c.ramping) continue;
            uint32_t elapsed = now_ms - c.start_ms;
            if (elapsed >= c.ramp_ms) {
                c.ramping = false;
                set(ch, c.target_us);
                done(ch, c.target_us);
            } else {
                int delta = static_cast<int>(c.target_us) - c.start_us;
                set(ch, static_cast<uint16_t>(c.start_us + delta * static_cast<int>(elapsed) / static_cast<int>(c.ramp_ms)));
            }
        }
    }

private:
    struct Channel {
        uint16_t min_us = 0, max_us = 0;  // max 0 = no LIMIT yet
        uint16_t pulse_us = 0;            // what the pin outputs now; 0 = relaxed
        uint16_t start_us = 0, target_us = 0;
        uint32_t start_ms = 0, ramp_ms = 0;
        bool ramping = false;
    };

    void set(int ch, uint16_t us);

    Channel channels_[count];
};

} // namespace pico_motion

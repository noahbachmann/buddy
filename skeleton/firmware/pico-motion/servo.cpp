#include "servo.hpp"

#include "buddy/motion_protocol.hpp"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

namespace pico_motion {

void Servos::init() {
    for (uint8_t pin : servo_pins) {
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, static_cast<float>(clock_get_hz(clk_sys)) / 1'000'000.0f);  // 1 MHz tick
        pwm_config_set_wrap(&cfg, 19'999);                                                       // 20 ms period
        pwm_init(pwm_gpio_to_slice_num(pin), &cfg, true);  // resets both compare levels to 0: no pulses
        gpio_set_function(pin, GPIO_FUNC_PWM);
    }
}

bool Servos::any_active() const {
    for (const auto& c : channels_)
        if (c.pulse_us != 0) return true;
    return false;
}

void Servos::limit(int ch, uint16_t min_us, uint16_t max_us) {
    auto& c = channels_[ch];
    c.min_us = min_us;
    c.max_us = max_us;
    if (c.pulse_us != 0 && (c.pulse_us < min_us || c.pulse_us > max_us)) relax(ch);
    else if (c.ramping) c.target_us = buddy::motion::clamp_pulse(c.target_us, min_us, max_us);
}

uint16_t Servos::move(int ch, uint16_t us, uint16_t ms, uint32_t now_ms) {
    auto& c = channels_[ch];
    uint16_t target = buddy::motion::clamp_pulse(us, c.min_us, c.max_us);
    if (c.pulse_us == 0) {
        set(ch, target);
        ms = 0;
    }
    c.start_us = c.pulse_us;
    c.target_us = target;
    c.start_ms = now_ms;
    c.ramp_ms = ms;
    c.ramping = true;
    return target;
}

void Servos::relax(int ch) {
    channels_[ch].ramping = false;
    set(ch, 0);
}

void Servos::relax_all() {
    for (int ch = 0; ch < count; ++ch) relax(ch);
}

void Servos::set(int ch, uint16_t us) {
    channels_[ch].pulse_us = us;
    pwm_set_gpio_level(servo_pins[ch], us);
}

} // namespace pico_motion

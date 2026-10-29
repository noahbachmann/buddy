// Buddy motion controller: protocol v1 lines from the Pi (USB CDC) → servo PWM. Guide 2.2, steps 5–6.
//
// Safety: channels start without pulses; a channel moves only after LIMIT; no line for 2 s relaxes everything
// (Pi or service gone); a main loop stuck for 500 ms resets the Pico (hardware watchdog) → back to no pulses.

#include "servo.hpp"

#include "buddy/motion_protocol.hpp"

#include "hardware/watchdog.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include <cstddef>
#include <string_view>

namespace bm = buddy::motion;
using pico_motion::Servos;

namespace {

constexpr uint32_t tick_ms = 20;  // one PWM period
constexpr uint32_t watchdog_ms = 500;
constexpr int max_chars_per_loop = 256;  // bounded drain: a flood of input can't starve the watchdog

Servos servos;

uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

void send(std::string_view line) {
    stdio_put_string(line.data(), static_cast<int>(line.size()), false, true);  // \n → \r\n for terminals
}

std::string_view handle(std::string_view text, bm::Line& out) {
    auto parsed = bm::parse_request(text);
    if (auto* rej = std::get_if<bm::Rejected>(&parsed)) return bm::format_error(rej->id, rej->error, out);
    const auto& r = std::get<bm::Request>(parsed);

    const bool all = r.op == bm::Op::relax && r.channel == bm::all_channels;
    const bool has_channel = r.op == bm::Op::limit || r.op == bm::Op::move || (r.op == bm::Op::relax && !all);
    if (has_channel && !Servos::exists(r.channel)) return bm::format_error(r.id, bm::Error::bad_channel, out);

    switch (r.op) {
        case bm::Op::hello:
            return bm::format_hello(r.id, BUDDY_MOTION_VERSION, out);
        case bm::Op::ping:
            break;
        case bm::Op::limit:
            servos.limit(r.channel, r.a, r.b);
            break;
        case bm::Op::move: {
            if (!servos.limited(r.channel)) return bm::format_error(r.id, bm::Error::no_limit, out);
            uint16_t target = servos.move(r.channel, r.a, r.b, now_ms());
            if (target != r.a) return bm::format_clamped(r.id, target, out);
            break;
        }
        case bm::Op::relax:
            if (all) servos.relax_all(); else servos.relax(r.channel);
            break;
    }
    return bm::format_ok(r.id, out);
}

} // namespace

int main() {
    const bool watchdog_reset = watchdog_enable_caused_reboot();
    stdio_init_all();
    servos.init();
    watchdog_enable(watchdog_ms, true);  // true: paused while a debugger halts the core

    bm::Line in{};
    std::size_t len = 0;
    bool overflow = false;  // drop the rest of a line longer than max_line
    bool boot_sent = false;
    uint32_t last_line_ms = now_ms();
    uint32_t next_tick_ms = now_ms();
    bm::Line out;

    while (true) {
        watchdog_update();
        const uint32_t now = now_ms();

        // Sent on the first connection after a reset: the host was gone while it happened, nobody would read it earlier.
        if (!boot_sent && stdio_usb_connected()) {
            send(bm::format_boot(BUDDY_MOTION_VERSION, watchdog_reset, out));
            boot_sent = true;
        }

        for (int i = 0; i < max_chars_per_loop; ++i) {
            int c = stdio_getchar_timeout_us(0);
            if (c == PICO_ERROR_TIMEOUT) break;
            if (c == '\n' || c == '\r') {  // picocom sends \r or \r\n; empty lines are ignored
                if (overflow) send(bm::format_error(0, bm::Error::bad_args, out));
                else if (len > 0) send(handle({in.data(), len}, out));
                if (overflow || len > 0) last_line_ms = now;
                len = 0;
                overflow = false;
            } else if (len < in.size() - 1) {
                in[len++] = static_cast<char>(c);
            } else {
                overflow = true;
            }
        }

        if (now - last_line_ms >= bm::link_timeout_ms && servos.any_active()) {
            servos.relax_all();
            send(bm::format_relaxed("link", out));
        }

        if (now - next_tick_ms < 0x8000'0000u) {  // now ≥ next_tick_ms, wrap-safe
            servos.update(now, [&](int ch, uint16_t us) { send(bm::format_done(ch, us, out)); });
            next_tick_ms = now + tick_ms;
        }

        sleep_ms(1);
    }
}

#pragma once
// Protocol v1 between the motion service (Pi) and firmware/pico-motion (Pico 2), over USB CDC. One ASCII line per
// message, '\n'-terminated ('\r' tolerated). Shared by both sides: plain C++20, no heap, no exceptions, no I/O.
//
//   Pi → Pico   <id> HELLO | LIMIT <ch> <min_us> <max_us> | MOVE <ch> <us> <ms> | RELAX <ch>|* | PING
//   Pico → Pi   <id> OK [buddy-motion <fw> <protocol>] | <id> OK clamped <us> | <id> ERR <reason>
//               * DONE <ch> <us> | * RELAXED <reason> | * BOOT <fw> [watchdog]
//
// Guide: docs/guides/02-02-motion-controller.md, step 6.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace buddy::motion {

inline constexpr int protocol_version = 1;
inline constexpr std::size_t max_line = 64;   // incl. '\n'; longer lines are rejected whole
inline constexpr int channel_count = 16;      // protocol range; the board may wire fewer (firmware: bad-channel)
inline constexpr uint16_t hard_min_us = 500;  // no LIMIT may leave this range
inline constexpr uint16_t hard_max_us = 2500;
inline constexpr uint16_t max_ramp_ms = 10000;
inline constexpr int link_timeout_ms = 2000;  // Pico relaxes everything after this long without a line
inline constexpr int all_channels = -1;       // RELAX *

enum class Op : uint8_t { hello, limit, move, relax, ping };
enum class Error : uint8_t { bad_channel, no_limit, bad_args, unknown };

struct Request {
    uint16_t id = 0;   // 1–65535, echoed by the reply
    Op op = Op::ping;
    int channel = 0;   // limit, move, relax (all_channels for RELAX *)
    uint16_t a = 0;    // limit: min_us, move: us
    uint16_t b = 0;    // limit: max_us, move: ms
    bool operator==(const Request&) const = default;
};

// A line the Pico can't execute; it replies `<id> ERR <reason>` (id 0 if the id itself was unreadable).
struct Rejected {
    uint16_t id = 0;
    Error error = Error::bad_args;
    bool operator==(const Rejected&) const = default;
};

enum class Kind : uint8_t { ok, err, done, relaxed, boot };

// One line from the Pico. `text` points into the parsed line (or the caller's string when formatting).
struct Message {
    Kind kind = Kind::ok;
    uint16_t id = 0;            // ok, err
    Error error = Error::unknown;
    int channel = 0;            // done
    uint16_t us = 0;            // done: reached pulse; ok: clamped pulse (0 = not clamped)
    std::string_view text;      // ok to HELLO: fw version; relaxed: reason ("link"); boot: fw version
    bool watchdog = false;      // boot: the hardware watchdog reset the Pico (firmware hung)
    bool operator==(const Message&) const = default;
};

struct Hello {
    std::string_view firmware;
    int protocol = 0;
};

using Line = std::array<char, max_line>;

constexpr std::string_view error_name(Error e) {
    switch (e) {
        case Error::bad_channel: return "bad-channel";
        case Error::no_limit: return "no-limit";
        case Error::bad_args: return "bad-args";
        case Error::unknown: return "unknown";
    }
    return "unknown";
}

constexpr uint16_t clamp_pulse(uint16_t us, uint16_t min_us, uint16_t max_us) {
    return us < min_us ? min_us : us > max_us ? max_us : us;
}

namespace detail {

// Splits on spaces; strips a trailing "\r" / "\n".
struct Tokens {
    std::string_view rest;

    explicit constexpr Tokens(std::string_view line) : rest(line) {
        while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r')) rest.remove_suffix(1);
    }
    constexpr std::string_view next() {
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        std::size_t n = 0;
        while (n < rest.size() && rest[n] != ' ') ++n;
        auto t = rest.substr(0, n);
        rest.remove_prefix(n);
        return t;
    }
    constexpr bool done() {
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        return rest.empty();
    }
};

// Unsigned decimal in [lo, hi]. Not std::from_chars: that's constexpr only from C++23.
template <typename T>
constexpr std::optional<T> number(std::string_view s, T lo, T hi) {
    if (s.empty() || s.size() > 5) return std::nullopt;  // ≤ 65535
    long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + (c - '0');
    }
    if (v < lo || v > hi) return std::nullopt;
    return static_cast<T>(v);
}

// Appends to a Line; overflow truncates, so formatting can't write out of bounds (lines here are < 40 chars).
struct Writer {
    Line& buf;
    std::size_t n = 0;

    constexpr Writer& put(std::string_view s) {
        for (char c : s)
            if (n < buf.size()) buf[n++] = c;
        return *this;
    }
    constexpr Writer& put(unsigned v) {
        char tmp[10];
        int i = 0;
        do tmp[i++] = static_cast<char>('0' + v % 10); while ((v /= 10) != 0);
        while (i > 0) put(std::string_view(&tmp[--i], 1));
        return *this;
    }
    constexpr std::string_view end() {
        put("\n");
        return {buf.data(), n};
    }
};

} // namespace detail

// Pi → Pico. Checks syntax and ranges that don't depend on the board (channel < channel_count, LIMIT within the
// hard range and min < max, ramp ≤ max_ramp_ms). MOVE pulses outside the limits are clamped later, not rejected.
constexpr std::variant<Request, Rejected> parse_request(std::string_view line) {
    detail::Tokens t(line);
    auto id = detail::number<uint16_t>(t.next(), 1, 65535);
    if (!id) return Rejected{0, Error::bad_args};
    Request r{.id = *id};
    const auto bad = [&](Error e) { return Rejected{*id, e}; };

    auto cmd = t.next();
    auto channel = [&]() { return detail::number<int>(t.next(), 0, channel_count - 1); };
    if (cmd == "HELLO") {
        r.op = Op::hello;
    } else if (cmd == "PING") {
        r.op = Op::ping;
    } else if (cmd == "LIMIT") {
        r.op = Op::limit;
        auto ch = channel();
        if (!ch) return bad(Error::bad_channel);
        auto lo = detail::number<uint16_t>(t.next(), hard_min_us, hard_max_us);
        auto hi = detail::number<uint16_t>(t.next(), hard_min_us, hard_max_us);
        if (!lo || !hi || *lo >= *hi) return bad(Error::bad_args);
        r.channel = *ch;
        r.a = *lo;
        r.b = *hi;
    } else if (cmd == "MOVE") {
        r.op = Op::move;
        auto ch = channel();
        if (!ch) return bad(Error::bad_channel);
        auto us = detail::number<uint16_t>(t.next(), 0, 65535);
        auto ms = detail::number<uint16_t>(t.next(), 0, max_ramp_ms);
        if (!us || !ms) return bad(Error::bad_args);
        r.channel = *ch;
        r.a = *us;
        r.b = *ms;
    } else if (cmd == "RELAX") {
        r.op = Op::relax;
        auto arg = t.next();
        if (arg == "*") {
            r.channel = all_channels;
        } else {
            auto ch = detail::number<int>(arg, 0, channel_count - 1);
            if (!ch) return bad(Error::bad_channel);
            r.channel = *ch;
        }
    } else {
        return bad(Error::unknown);
    }
    if (!t.done()) return bad(Error::bad_args);
    return r;
}

constexpr std::string_view format_request(const Request& r, Line& buf) {
    detail::Writer w{buf};
    w.put(r.id).put(" ");
    switch (r.op) {
        case Op::hello: w.put("HELLO"); break;
        case Op::ping: w.put("PING"); break;
        case Op::limit: w.put("LIMIT ").put(unsigned(r.channel)).put(" ").put(r.a).put(" ").put(r.b); break;
        case Op::move: w.put("MOVE ").put(unsigned(r.channel)).put(" ").put(r.a).put(" ").put(r.b); break;
        case Op::relax:
            w.put("RELAX ");
            if (r.channel == all_channels) w.put("*"); else w.put(unsigned(r.channel));
            break;
    }
    return w.end();
}

// Pico → Pi. nullopt = not a protocol line (the service logs and ignores it).
constexpr std::optional<Message> parse_message(std::string_view line) {
    detail::Tokens t(line);
    auto head = t.next();
    Message m;
    if (head == "*") {
        auto ev = t.next();
        if (ev == "DONE") {
            auto ch = detail::number<int>(t.next(), 0, channel_count - 1);
            auto us = detail::number<uint16_t>(t.next(), 0, 65535);
            if (!ch || !us) return std::nullopt;
            m.kind = Kind::done;
            m.channel = *ch;
            m.us = *us;
        } else if (ev == "RELAXED") {
            m.kind = Kind::relaxed;
            m.text = t.next();
        } else if (ev == "BOOT") {
            m.kind = Kind::boot;
            m.text = t.next();
            if (m.text.empty()) return std::nullopt;
            auto reason = t.next();
            if (reason == "watchdog") m.watchdog = true;
            else if (!reason.empty()) return std::nullopt;
        } else {
            return std::nullopt;
        }
        return t.done() ? std::optional(m) : std::nullopt;
    }

    auto id = detail::number<uint16_t>(head, 0, 65535);
    if (!id) return std::nullopt;
    m.id = *id;
    auto status = t.next();
    if (status == "OK") {
        m.kind = Kind::ok;
        auto extra = t.next();
        if (extra == "clamped") {
            auto us = detail::number<uint16_t>(t.next(), 1, 65535);
            if (!us) return std::nullopt;
            m.us = *us;
        } else if (extra == "buddy-motion") {
            m.text = t.next();  // fw version; protocol version follows, read by parse_hello()
            t.next();
        } else if (!extra.empty()) {
            return std::nullopt;
        }
    } else if (status == "ERR") {
        m.kind = Kind::err;
        auto reason = t.next();
        if (reason == "bad-channel") m.error = Error::bad_channel;
        else if (reason == "no-limit") m.error = Error::no_limit;
        else if (reason == "bad-args") m.error = Error::bad_args;
        else m.error = Error::unknown;  // also covers reasons from a newer firmware
    } else {
        return std::nullopt;
    }
    return t.done() ? std::optional(m) : std::nullopt;
}

// The OK reply to HELLO: "<id> OK buddy-motion <fw> <protocol>".
constexpr std::optional<Hello> parse_hello(std::string_view line) {
    detail::Tokens t(line);
    t.next();
    if (t.next() != "OK" || t.next() != "buddy-motion") return std::nullopt;
    Hello h{.firmware = t.next()};
    auto p = detail::number<int>(t.next(), 1, 65535);
    if (h.firmware.empty() || !p || !t.done()) return std::nullopt;
    h.protocol = *p;
    return h;
}

constexpr std::string_view format_ok(uint16_t id, Line& buf) {
    return detail::Writer{buf}.put(id).put(" OK").end();
}

constexpr std::string_view format_clamped(uint16_t id, uint16_t us, Line& buf) {
    return detail::Writer{buf}.put(id).put(" OK clamped ").put(us).end();
}

constexpr std::string_view format_hello(uint16_t id, std::string_view firmware, Line& buf) {
    return detail::Writer{buf}.put(id).put(" OK buddy-motion ").put(firmware).put(" ").put(unsigned(protocol_version)).end();
}

constexpr std::string_view format_error(uint16_t id, Error e, Line& buf) {
    return detail::Writer{buf}.put(id).put(" ERR ").put(error_name(e)).end();
}

constexpr std::string_view format_done(int channel, uint16_t us, Line& buf) {
    return detail::Writer{buf}.put("* DONE ").put(unsigned(channel)).put(" ").put(us).end();
}

constexpr std::string_view format_relaxed(std::string_view reason, Line& buf) {
    return detail::Writer{buf}.put("* RELAXED ").put(reason).end();
}

constexpr std::string_view format_boot(std::string_view firmware, bool watchdog, Line& buf) {
    detail::Writer w{buf};
    w.put("* BOOT ").put(firmware);
    if (watchdog) w.put(" watchdog");
    return w.end();
}

} // namespace buddy::motion

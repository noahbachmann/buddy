#pragma once
// The Pi end of protocol v1: owns the serial port (/dev/buddy-pico), one request in flight at a time.
//
// A reader thread splits incoming lines: `<id> …` replies wake the waiting request(), `* …` lines go to events().
// open() does the handshake: HELLO (protocol version must match) → LIMIT per joint. The link counts as lost after
// a read/write error, a vanished device, or 3 reply timeouts in a row; the owner then closes and opens again.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

#include "buddy/motion_protocol.hpp"
#include "buddy/queue.hpp"
#include "joints.hpp"

namespace buddy::motion {

struct LinkError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Reply {
    Kind kind = Kind::ok;  // ok or err
    Error error = Error::unknown;
    uint16_t us = 0;  // ok: clamped pulse (0 = not clamped)
    std::string line{};
};

class SerialLink {
public:
    static constexpr auto reply_timeout = std::chrono::milliseconds(500);
    static constexpr int max_timeouts = 3;

    explicit SerialLink(bool debug = false) : debug_(debug) {}
    ~SerialLink() { close(); }
    SerialLink(const SerialLink&) = delete;
    SerialLink& operator=(const SerialLink&) = delete;

    // Opens the port and shakes hands. Returns the firmware version. Throws LinkError (port missing or busy,
    // no or wrong HELLO, a LIMIT refused); the port is closed again then.
    std::string open(const std::string& path, std::span<const Joint> limits);
    void close();
    bool is_open() const { return fd_ >= 0; }
    // Reader thread saw an error or the device vanished: close() and open() again.
    bool lost() const { return lost_.load(); }

    // Sends a request (the link sets its id) and waits for the reply. nullopt = no reply within reply_timeout.
    // Throws LinkError on a write error, a lost link or the max_timeouts-th timeout in a row.
    std::optional<Reply> request(Request r);
    // One hand-written protocol line with its own id (--raw): the reply line, nullopt on timeout.
    std::optional<std::string> raw(const std::string& line);

    // Unsolicited `* …` lines (DONE, RELAXED, BOOT), without the line end.
    BlockingQueue<std::string>& events() { return events_; }

private:
    void read_loop();
    void on_line(const std::string& line);
    void write_line(std::string_view line);
    std::optional<std::string> await_send(uint16_t id, std::string_view line);

    bool debug_;
    int fd_ = -1;
    std::thread reader_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> lost_{false};
    uint16_t next_id_ = 1;
    int timeouts_ = 0;

    std::mutex mutex_;  // guards the pending reply
    std::condition_variable cv_;
    uint16_t waiting_for_ = 0;
    std::optional<std::string> reply_;

    BlockingQueue<std::string> events_;
};

} // namespace buddy::motion

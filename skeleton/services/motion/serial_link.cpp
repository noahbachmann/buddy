#include "serial_link.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

namespace buddy::motion {

namespace {

std::string errno_text(const std::string& what) { return what + ": " + std::strerror(errno); }

// "17 OK" → 17; 0 for lines without a numeric id
uint16_t reply_id(std::string_view line) {
    const auto n = detail::number<uint16_t>(line.substr(0, line.find(' ')), 1, 65535);
    return n ? *n : 0;
}

} // namespace

std::string SerialLink::open(const std::string& path, std::span<const Joint> limits) {
    close();
    fd_ = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if (fd_ < 0) throw LinkError(errno_text(path));
    // picocom and a second buddy-motion lock the port too: fail instead of sharing it
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        const auto e = errno_text(path);
        ::close(fd_);
        fd_ = -1;
        throw LinkError(errno == EWOULDBLOCK ? path + ": busy (picocom or another buddy-motion?)" : e);
    }
    termios tio{};
    if (tcgetattr(fd_, &tio) == 0) {  // CDC ignores the baud rate; raw mode matters (no echo, no line editing)
        cfmakeraw(&tio);
        cfsetspeed(&tio, B115200);
        tio.c_cflag |= CLOCAL | CREAD;
        tcsetattr(fd_, TCSANOW, &tio);
    }
    tcflush(fd_, TCIOFLUSH);  // stale replies from an earlier session

    stop_ = false;
    lost_ = false;
    timeouts_ = 0;
    reader_ = std::thread(&SerialLink::read_loop, this);

    try {
        const auto hello = request({.op = Op::hello});
        if (!hello) throw LinkError("no reply to HELLO");
        const auto h = parse_hello(hello->line);
        if (!h) throw LinkError("unexpected HELLO reply: " + hello->line);
        if (h->protocol != protocol_version)
            throw LinkError("protocol version " + std::to_string(h->protocol) + ", need " +
                            std::to_string(protocol_version) + ": deploy/deploy.sh --firmware");
        std::string firmware(h->firmware);
        for (const auto& j : limits) {
            const auto r = request({.op = Op::limit, .channel = j.channel, .a = j.min_us, .b = j.max_us});
            if (!r || r->kind != Kind::ok)
                throw LinkError("LIMIT " + std::string(j.name) + " refused: " + (r ? r->line : "no reply"));
        }
        // `* BOOT` arrives with the first connection after a reset, before the handshake's replies: not a reset
        // during this session. Drop it and whatever else was queued.
        while (auto e = events_.pop(std::chrono::milliseconds(0)))
            if (e->starts_with("* BOOT") && e->ends_with("watchdog"))
                std::cout << "[motion] the Pico's hardware watchdog had reset it (firmware hung)" << std::endl;
        return firmware;
    } catch (...) {
        close();
        throw;
    }
}

void SerialLink::close() {
    stop_ = true;
    if (reader_.joinable()) reader_.join();
    if (fd_ >= 0) ::close(fd_);  // releases the flock
    fd_ = -1;
    while (events_.pop(std::chrono::milliseconds(0))) {
    }
}

std::optional<Reply> SerialLink::request(Request r) {
    if (fd_ < 0 || lost_) throw LinkError("link lost");
    r.id = next_id_;
    next_id_ = next_id_ == 65535 ? 1 : next_id_ + 1;
    Line buf;
    const auto line = await_send(r.id, format_request(r, buf));
    if (!line) {
        if (++timeouts_ >= max_timeouts) {
            lost_ = true;
            throw LinkError("no reply " + std::to_string(max_timeouts) + " times in a row");
        }
        return std::nullopt;
    }
    timeouts_ = 0;
    const auto m = parse_message(*line);
    Reply reply{.line = *line};
    if (m) {
        reply.kind = m->kind;
        reply.error = m->error;
        reply.us = m->us;
    } else {
        reply.kind = Kind::err;  // not a protocol line
    }
    return reply;
}

std::optional<std::string> SerialLink::raw(const std::string& line) {
    if (fd_ < 0 || lost_) throw LinkError("link lost");
    return await_send(reply_id(line), line + "\n");
}

std::optional<std::string> SerialLink::await_send(uint16_t id, std::string_view line) {
    std::unique_lock lock(mutex_);
    waiting_for_ = id;
    reply_.reset();
    lock.unlock();
    write_line(line);
    lock.lock();
    cv_.wait_for(lock, reply_timeout, [&] { return reply_.has_value() || lost_; });
    waiting_for_ = 0;
    if (lost_ && !reply_) throw LinkError("link lost");
    return std::exchange(reply_, std::nullopt);
}

void SerialLink::write_line(std::string_view line) {
    if (debug_) std::cout << "[motion] → " << line.substr(0, line.size() - 1) << std::endl;
    while (!line.empty()) {
        const ssize_t n = ::write(fd_, line.data(), line.size());
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno == EAGAIN) {  // USB busy: lines are tiny, wait briefly
            pollfd p{.fd = fd_, .events = POLLOUT, .revents = 0};
            if (poll(&p, 1, 100) > 0) continue;
        }
        if (n <= 0) {
            lost_ = true;
            throw LinkError(errno_text("write"));
        }
        line.remove_prefix(static_cast<std::size_t>(n));
    }
}

void SerialLink::read_loop() {
    std::string pending;
    char buf[256];
    while (!stop_) {
        pollfd p{.fd = fd_, .events = POLLIN, .revents = 0};
        const int r = poll(&p, 1, 100);
        if (r == 0 || (r < 0 && errno == EINTR)) continue;
        ssize_t n = -1;
        errno = 0;
        if (r > 0 && (p.revents & POLLIN)) n = ::read(fd_, buf, sizeof buf);  // else HUP/ERR: device gone
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (n <= 0) {  // unplugged (EIO/HUP) or closed
            lost_ = true;
            std::lock_guard lock(mutex_);  // no missed wakeup in await_send()
            cv_.notify_all();
            return;
        }
        pending.append(buf, static_cast<std::size_t>(n));
        for (auto at = pending.find('\n'); at != std::string::npos; at = pending.find('\n')) {
            std::string line = pending.substr(0, at);
            pending.erase(0, at + 1);
            while (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) on_line(line);
        }
        if (pending.size() > 4 * max_line) pending.clear();  // garbage without line ends
    }
}

void SerialLink::on_line(const std::string& line) {
    if (debug_) std::cout << "[motion] ← " << line << std::endl;
    if (line.starts_with("*")) {
        events_.push(line);
        return;
    }
    const uint16_t id = reply_id(line);
    std::lock_guard lock(mutex_);
    if (id != 0 && id == waiting_for_) {
        reply_ = line;
        cv_.notify_all();
    } else if (!debug_) {
        std::cout << "[motion] unexpected line from the Pico: " << line << std::endl;
    }
}

} // namespace buddy::motion

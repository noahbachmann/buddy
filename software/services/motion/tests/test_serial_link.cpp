// serial_link against a fake Pico on a pseudo-terminal: the link opens the pty's slave path like /dev/buddy-pico,
// the fake answers on the master side with the shared protocol code (the same calls the firmware makes).
#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <pty.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <poll.h>

#include "serial_link.hpp"

using namespace buddy::motion;
using namespace std::chrono_literals;

namespace {

class FakePico {
public:
    // answer(request line) → reply line ("" = stay silent); default: what the firmware answers
    std::function<std::string(const std::string&)> answer = [this](const std::string& line) { return firmware(line); };

    FakePico() {
        char name[256];
        if (openpty(&master_, &slave_, name, nullptr, nullptr) != 0) throw std::runtime_error("openpty");
        path_ = name;
        thread_ = std::thread([this] { run(); });
    }
    ~FakePico() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        if (master_ >= 0) ::close(master_);
        ::close(slave_);
    }

    const std::string& path() const { return path_; }
    void send(const std::string& line) { (void)!::write(master_, line.data(), line.size()); }
    void unplug() {  // the slave side sees EIO/HUP like a vanished USB device
        stop_ = true;
        thread_.join();
        ::close(master_);
        master_ = -1;
    }
    std::vector<std::string> received() {
        std::lock_guard lock(mutex_);
        return received_;
    }

    std::string firmware(const std::string& line) {
        Line out;
        const auto parsed = parse_request(line);
        if (const auto* rej = std::get_if<Rejected>(&parsed)) return std::string(format_error(rej->id, rej->error, out));
        const auto& r = std::get<Request>(parsed);
        if (r.op == Op::hello) return std::string(format_hello(r.id, "test", out));
        return std::string(format_ok(r.id, out));
    }

private:
    void run() {
        std::string pending;
        char buf[256];
        while (!stop_) {
            pollfd p{.fd = master_, .events = POLLIN, .revents = 0};
            if (poll(&p, 1, 20) <= 0) continue;
            const ssize_t n = ::read(master_, buf, sizeof buf);
            if (n <= 0) continue;
            pending.append(buf, static_cast<std::size_t>(n));
            for (auto at = pending.find('\n'); at != std::string::npos; at = pending.find('\n')) {
                const std::string line = pending.substr(0, at);
                pending.erase(0, at + 1);
                {
                    std::lock_guard lock(mutex_);
                    received_.push_back(line);
                }
                if (const auto reply = answer(line); !reply.empty()) send(reply);
            }
        }
    }

    int master_ = -1, slave_ = -1;  // slave_ stays open: otherwise the master reads EIO between link sessions
    std::string path_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<std::string> received_;
};

// "17 LIMIT 0 700 2300" → "LIMIT 0 700 2300"
std::string without_id(const std::string& line) { return line.substr(line.find(' ') + 1); }

} // namespace

TEST(MotionLink, HandshakeSendsEveryLimit) {
    FakePico pico;
    SerialLink link;
    EXPECT_EQ(link.open(pico.path(), joints()), "test");
    EXPECT_TRUE(link.is_open());
    const auto got = pico.received();
    ASSERT_EQ(got.size(), 1 + joints().size());
    EXPECT_EQ(without_id(got[0]), "HELLO");
    for (std::size_t i = 0; i < joints().size(); ++i) {
        const auto& j = joints()[i];
        EXPECT_EQ(without_id(got[i + 1]), "LIMIT " + std::to_string(j.channel) + " " + std::to_string(j.min_us) + " " +
                                              std::to_string(j.max_us));
    }
}

TEST(MotionLink, RepliesMatchTheirRequest) {
    FakePico pico;
    pico.answer = [&](const std::string& line) {
        if (line.ends_with("MOVE 0 2400 300")) {
            pico.send("* DONE 1 1500\n");  // an event in between doesn't confuse the reply matching
            return line.substr(0, line.find(' ')) + " OK clamped 2300\n";
        }
        return pico.firmware(line);
    };
    SerialLink link;
    link.open(pico.path(), joints());
    const auto r = link.request({.op = Op::move, .channel = 0, .a = 2400, .b = 300});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->kind, Kind::ok);
    EXPECT_EQ(r->us, 2300);
    EXPECT_EQ(link.events().pop(500ms), "* DONE 1 1500");

    const auto e = link.request({.op = Op::move, .channel = 15, .a = 1500, .b = 0});
    ASSERT_TRUE(e);  // the fake answers OK: a channel error comes from the firmware's board check, not here
    EXPECT_EQ(e->kind, Kind::ok);
}

TEST(MotionLink, ErrorReplies) {
    FakePico pico;
    pico.answer = [&](const std::string& line) {
        Line out;
        if (line.ends_with("PING")) return std::string(format_error(std::stoi(line), Error::no_limit, out));
        return pico.firmware(line);
    };
    SerialLink link;
    link.open(pico.path(), joints());
    const auto r = link.request({.op = Op::ping});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->kind, Kind::err);
    EXPECT_EQ(r->error, Error::no_limit);
}

TEST(MotionLink, WrongProtocolVersionFailsTheHandshake) {
    FakePico pico;
    pico.answer = [](const std::string& line) {
        return line.ends_with("HELLO") ? line.substr(0, line.find(' ')) + " OK buddy-motion 9.9 2\n" : "";
    };
    SerialLink link;
    EXPECT_THROW(link.open(pico.path(), joints()), LinkError);
    EXPECT_FALSE(link.is_open());
}

TEST(MotionLink, RefusedLimitFailsTheHandshake) {
    FakePico pico;
    pico.answer = [&](const std::string& line) {
        Line out;
        if (line.find("LIMIT") != std::string::npos) return std::string(format_error(std::stoi(line), Error::bad_channel, out));
        return pico.firmware(line);
    };
    SerialLink link;
    EXPECT_THROW(link.open(pico.path(), joints()), LinkError);
}

TEST(MotionLink, MissingPortThrows) {
    SerialLink link;
    EXPECT_THROW(link.open("/nonexistent/buddy-pico", joints()), LinkError);
    EXPECT_FALSE(link.is_open());
}

TEST(MotionLink, TimeoutsThenDisconnect) {
    FakePico pico;
    SerialLink link;
    link.open(pico.path(), joints());
    pico.answer = [](const std::string&) { return std::string(); };  // hung Pico
    EXPECT_FALSE(link.request({.op = Op::ping}));
    EXPECT_FALSE(link.request({.op = Op::ping}));
    EXPECT_THROW(link.request({.op = Op::ping}), LinkError);
    EXPECT_TRUE(link.lost());
}

TEST(MotionLink, ATimeoutInBetweenIsForgiven) {
    FakePico pico;
    SerialLink link;
    link.open(pico.path(), joints());
    std::atomic<bool> silent{true};
    pico.answer = [&](const std::string& line) { return silent ? std::string() : pico.firmware(line); };
    EXPECT_FALSE(link.request({.op = Op::ping}));
    EXPECT_FALSE(link.request({.op = Op::ping}));
    silent = false;
    EXPECT_TRUE(link.request({.op = Op::ping}));
    silent = true;
    EXPECT_FALSE(link.request({.op = Op::ping}));  // the counter restarted
    EXPECT_FALSE(link.lost());
}

TEST(MotionLink, BootDuringHandshakeIsDropped) {
    FakePico pico;
    pico.answer = [&](const std::string& line) {
        if (line.ends_with("HELLO")) pico.send("* BOOT test\n");  // first connection after a reset
        return pico.firmware(line);
    };
    SerialLink link;
    link.open(pico.path(), joints());
    EXPECT_FALSE(link.events().pop(100ms));

    pico.send("* BOOT test watchdog\n");  // later: the Pico reset under us; the service re-handshakes
    EXPECT_EQ(link.events().pop(500ms), "* BOOT test watchdog");
    link.close();
    pico.answer = [&](const std::string& line) { return pico.firmware(line); };
    EXPECT_EQ(link.open(pico.path(), joints()), "test");
}

TEST(MotionLink, UnplugIsNoticed) {
    FakePico pico;
    SerialLink link;
    link.open(pico.path(), joints());
    pico.unplug();
    for (int i = 0; i < 50 && !link.lost(); ++i) std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(link.lost());
    EXPECT_THROW(link.request({.op = Op::ping}), LinkError);
}

TEST(MotionLink, SecondOpenerIsRefused) {  // picocom or a CLion run holding the port
    FakePico pico;
    SerialLink a, b;
    a.open(pico.path(), joints());
    EXPECT_THROW(b.open(pico.path(), joints()), LinkError);
}

TEST(MotionLink, RawLine) {
    FakePico pico;
    SerialLink link;
    link.open(pico.path(), joints());
    EXPECT_EQ(link.raw("77 MOVE 0 1500 300"), "77 OK");
}

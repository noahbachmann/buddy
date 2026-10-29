#include <gtest/gtest.h>

#include "buddy/motion_protocol.hpp"

#include <string>

using namespace buddy::motion;

namespace {

Request req(std::string_view line) {
    auto r = parse_request(line);
    EXPECT_TRUE(std::holds_alternative<Request>(r)) << line;
    return std::holds_alternative<Request>(r) ? std::get<Request>(r) : Request{};
}

Rejected rejected(std::string_view line) {
    auto r = parse_request(line);
    EXPECT_TRUE(std::holds_alternative<Rejected>(r)) << line;
    return std::holds_alternative<Rejected>(r) ? std::get<Rejected>(r) : Rejected{};
}

std::string str(std::string_view s) { return std::string(s); }

} // namespace

// Parsing is constexpr: the firmware can't throw, the checks hold at compile time too.
static_assert(std::get<Request>(parse_request("7 MOVE 1 1500 300\n")) ==
              Request{.id = 7, .op = Op::move, .channel = 1, .a = 1500, .b = 300});

TEST(MotionProtocol, ParsesRequests) {
    EXPECT_EQ(req("1 HELLO"), (Request{.id = 1, .op = Op::hello}));
    EXPECT_EQ(req("65535 PING\r\n"), (Request{.id = 65535, .op = Op::ping}));
    EXPECT_EQ(req("2 LIMIT 0 1000 2000"), (Request{.id = 2, .op = Op::limit, .channel = 0, .a = 1000, .b = 2000}));
    EXPECT_EQ(req("3 MOVE 15 2400 0"), (Request{.id = 3, .op = Op::move, .channel = 15, .a = 2400, .b = 0}));
    EXPECT_EQ(req("4 RELAX 3"), (Request{.id = 4, .op = Op::relax, .channel = 3}));
    EXPECT_EQ(req("5 RELAX *"), (Request{.id = 5, .op = Op::relax, .channel = all_channels}));
    EXPECT_EQ(req("  6   MOVE  0 1500   10  "), (Request{.id = 6, .op = Op::move, .channel = 0, .a = 1500, .b = 10}));
}

TEST(MotionProtocol, RejectsMalformedRequests) {
    EXPECT_EQ(rejected(""), (Rejected{0, Error::bad_args}));
    EXPECT_EQ(rejected("HELLO"), (Rejected{0, Error::bad_args}));
    EXPECT_EQ(rejected("0 HELLO"), (Rejected{0, Error::bad_args}));       // ids start at 1
    EXPECT_EQ(rejected("65536 HELLO"), (Rejected{0, Error::bad_args}));
    EXPECT_EQ(rejected("-1 HELLO"), (Rejected{0, Error::bad_args}));
    EXPECT_EQ(rejected("1 hello"), (Rejected{1, Error::unknown}));        // commands are upper case
    EXPECT_EQ(rejected("1 JUMP 0"), (Rejected{1, Error::unknown}));
    EXPECT_EQ(rejected("1 PING extra"), (Rejected{1, Error::bad_args}));
    EXPECT_EQ(rejected("2 MOVE 16 1500 0"), (Rejected{2, Error::bad_channel}));
    EXPECT_EQ(rejected("2 MOVE x 1500 0"), (Rejected{2, Error::bad_channel}));
    EXPECT_EQ(rejected("2 MOVE 0 1500"), (Rejected{2, Error::bad_args}));
    EXPECT_EQ(rejected("2 MOVE 0 1500 10001"), (Rejected{2, Error::bad_args}));  // > max_ramp_ms
    EXPECT_EQ(rejected("2 MOVE 0 +1500 0"), (Rejected{2, Error::bad_args}));
    EXPECT_EQ(rejected("3 RELAX"), (Rejected{3, Error::bad_channel}));
}

TEST(MotionProtocol, LimitStaysInHardRange) {
    EXPECT_EQ(req("1 LIMIT 0 500 2500").b, 2500);
    EXPECT_EQ(rejected("1 LIMIT 0 499 2000"), (Rejected{1, Error::bad_args}));
    EXPECT_EQ(rejected("1 LIMIT 0 1000 2501"), (Rejected{1, Error::bad_args}));
    EXPECT_EQ(rejected("1 LIMIT 0 1500 1500"), (Rejected{1, Error::bad_args}));  // min < max
    EXPECT_EQ(rejected("1 LIMIT 0 2000 1000"), (Rejected{1, Error::bad_args}));
}

TEST(MotionProtocol, MovePulseIsClampedNotRejected) {
    EXPECT_EQ(req("1 MOVE 0 3000 0").a, 3000);
    EXPECT_EQ(clamp_pulse(3000, 1000, 2000), 2000);
    EXPECT_EQ(clamp_pulse(0, 1000, 2000), 1000);
    EXPECT_EQ(clamp_pulse(1500, 1000, 2000), 1500);
}

TEST(MotionProtocol, RequestRoundTrip) {
    const Request requests[] = {
        {.id = 1, .op = Op::hello},
        {.id = 2, .op = Op::ping},
        {.id = 3, .op = Op::limit, .channel = 7, .a = 600, .b = 2400},
        {.id = 400, .op = Op::move, .channel = 15, .a = 1234, .b = 10000},
        {.id = 5, .op = Op::relax, .channel = 0},
        {.id = 65535, .op = Op::relax, .channel = all_channels},
    };
    for (const auto& r : requests) {
        Line buf;
        auto line = format_request(r, buf);
        ASSERT_EQ(line.back(), '\n');
        EXPECT_EQ(req(line), r) << line;
    }
    Line buf;
    EXPECT_EQ(str(format_request({.id = 9, .op = Op::move, .channel = 1, .a = 1500, .b = 300}, buf)), "9 MOVE 1 1500 300\n");
}

TEST(MotionProtocol, FormatsReplies) {
    Line buf;
    EXPECT_EQ(str(format_ok(3, buf)), "3 OK\n");
    EXPECT_EQ(str(format_clamped(5, 2000, buf)), "5 OK clamped 2000\n");
    EXPECT_EQ(str(format_hello(1, "0.1.0", buf)), "1 OK buddy-motion 0.1.0 1\n");
    EXPECT_EQ(str(format_error(0, Error::bad_args, buf)), "0 ERR bad-args\n");
    EXPECT_EQ(str(format_error(8, Error::no_limit, buf)), "8 ERR no-limit\n");
    EXPECT_EQ(str(format_done(0, 1000, buf)), "* DONE 0 1000\n");
    EXPECT_EQ(str(format_relaxed("link", buf)), "* RELAXED link\n");
    EXPECT_EQ(str(format_boot("0.1.0", false, buf)), "* BOOT 0.1.0\n");
    EXPECT_EQ(str(format_boot("0.1.0", true, buf)), "* BOOT 0.1.0 watchdog\n");
}

TEST(MotionProtocol, MessageRoundTrip) {
    Line buf;
    EXPECT_EQ(parse_message(format_ok(3, buf)), (Message{.kind = Kind::ok, .id = 3}));
    EXPECT_EQ(parse_message(format_clamped(5, 2000, buf)), (Message{.kind = Kind::ok, .id = 5, .us = 2000}));
    EXPECT_EQ(parse_message(format_error(8, Error::no_limit, buf)),
              (Message{.kind = Kind::err, .id = 8, .error = Error::no_limit}));
    EXPECT_EQ(parse_message(format_done(15, 2500, buf)), (Message{.kind = Kind::done, .channel = 15, .us = 2500}));

    auto relaxed = parse_message(format_relaxed("link", buf));
    ASSERT_TRUE(relaxed);
    EXPECT_EQ(relaxed->kind, Kind::relaxed);
    EXPECT_EQ(relaxed->text, "link");

    auto boot = parse_message(format_boot("0.1.0", true, buf));
    ASSERT_TRUE(boot);
    EXPECT_EQ(boot->kind, Kind::boot);
    EXPECT_EQ(boot->text, "0.1.0");
    EXPECT_TRUE(boot->watchdog);

    auto line = format_hello(1, "0.1.0", buf);
    auto hello = parse_message(line);
    ASSERT_TRUE(hello);
    EXPECT_EQ(hello->kind, Kind::ok);
    EXPECT_EQ(hello->text, "0.1.0");
    auto h = parse_hello(line);
    ASSERT_TRUE(h);
    EXPECT_EQ(h->firmware, "0.1.0");
    EXPECT_EQ(h->protocol, protocol_version);
}

TEST(MotionProtocol, RejectsMalformedMessages) {
    EXPECT_FALSE(parse_message(""));
    EXPECT_FALSE(parse_message("hello from the bootloader"));
    EXPECT_FALSE(parse_message("1 MAYBE"));
    EXPECT_FALSE(parse_message("1 OK whatever"));
    EXPECT_FALSE(parse_message("1 OK clamped"));
    EXPECT_FALSE(parse_message("* DONE 0"));
    EXPECT_FALSE(parse_message("* BOOT"));
    EXPECT_FALSE(parse_message("* BOOT 0.1.0 brownout"));
    EXPECT_FALSE(parse_message("* NEWS"));
    EXPECT_FALSE(parse_hello("1 OK"));
    EXPECT_FALSE(parse_hello("1 OK buddy-motion 0.1.0"));
    // A newer firmware's error reason still parses as an error.
    EXPECT_EQ(parse_message("4 ERR overheated"), (Message{.kind = Kind::err, .id = 4, .error = Error::unknown}));
}

TEST(MotionProtocol, LongestLinesFit) {
    Line buf;
    EXPECT_LT(format_request({.id = 65535, .op = Op::limit, .channel = 15, .a = 2500, .b = 2500}, buf).size(), max_line);
    EXPECT_LT(format_hello(65535, "10.10.10-dirty", buf).size(), max_line);
}

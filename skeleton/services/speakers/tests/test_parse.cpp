// tests/data: captured on the Pi (2026-10-30, MACs replaced). Main speaker connected, music output unlinked, a
// Soloist stream added to sink-inputs.json as seen while it played.
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "parse.hpp"

namespace buddy::speakers {
namespace {

std::string data(const char* name) {
    std::ifstream in(std::string(BUDDY_SOURCE_DIR "/services/speakers/tests/data/") + name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST(Parse, SinksWithTheBluetoothAddress) {
    const auto sinks = parse_sinks(data("sinks.json"));
    ASSERT_EQ(sinks.size(), 4u);
    EXPECT_EQ(sinks[0].name, "buddy_music");
    EXPECT_EQ(sinks[0].mac, "");
    EXPECT_EQ(sinks[3].name, "bluez_output.00_11_22_33_44_01.1");
    EXPECT_EQ(sinks[3].mac, "00:11:22:33:44:01");
}

TEST(Parse, StreamsLinkedUnlinkedAndCorked) {
    const auto streams = parse_streams(data("sink-inputs.json"));
    ASSERT_EQ(streams.size(), 3u);
    EXPECT_EQ(streams[0].node_name, "buddy_music.output");
    EXPECT_EQ(streams[0].sink, -1);  // PA_INVALID_INDEX: autoconnect off, not linked
    EXPECT_TRUE(streams[0].corked);
    EXPECT_EQ(streams[1].node_name, "buddy_voice.output");
    EXPECT_GE(streams[1].sink, 0);
    EXPECT_EQ(streams[2].node_name, "");  // Soloist
    EXPECT_EQ(streams[2].sink, parse_sinks(data("sinks.json"))[0].index);
    EXPECT_FALSE(streams[2].corked);
}

TEST(Parse, BrokenPactlOutputThrows) {
    EXPECT_THROW(parse_sinks(""), std::runtime_error);
    EXPECT_THROW(parse_streams("Connection failure: Connection refused"), std::runtime_error);
    EXPECT_TRUE(parse_sinks("[]").empty());
}

TEST(Parse, Devices) {
    const auto devices = parse_devices(data("devices.txt"));
    ASSERT_EQ(devices.size(), 2u);
    EXPECT_EQ(devices[0].mac, "00:11:22:33:44:02");
    EXPECT_EQ(devices[0].name, "Bathroom speaker haha");
    EXPECT_EQ(devices[1].name, "Main speaker");
}

TEST(Parse, DevicesSkipColouredNoise) {
    const auto devices = parse_devices("\x1b[0;93m[CHG]\x1b[0m Device 00:11:22:33:44:01 Connected: yes\n"
                                       "Device 00:11:22:33:44:01 Main speaker\r\n"
                                       "Agent registered\n");
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices[0].name, "Main speaker");
}

TEST(Parse, Info) {
    const auto main = parse_info(data("info-main.txt"));
    EXPECT_EQ(main.alias, "Main speaker");
    EXPECT_TRUE(main.connected);
    EXPECT_TRUE(main.audio_sink);
    const auto mini = parse_info(data("info-mini.txt"));
    EXPECT_EQ(mini.alias, "Bathroom speaker haha");
    EXPECT_FALSE(mini.connected);
    EXPECT_TRUE(mini.audio_sink);
    EXPECT_FALSE(parse_info("Device 00:11:22:33:44:09 not available\n").audio_sink);
}

} // namespace
} // namespace buddy::speakers

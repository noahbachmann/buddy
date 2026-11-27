// Parsers for the tools speakers drives: `pactl -f json list sinks|sink-inputs` (PipeWire via pipewire-pulse) and
// `bluetoothctl devices|info` (BlueZ). No I/O here: main.cpp runs the tools, the tests feed captured output.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace buddy::speakers {

struct Sink {
    int index = -1;
    std::string name;  // node name, e.g. buddy_music, bluez_output.AA_BB_CC_DD_EE_FF.1
    std::string mac;   // api.bluez5.address: set for Bluetooth sinks only
};

struct Stream {
    int index = -1;
    int sink = -1;          // -1 = not linked
    std::string node_name;  // e.g. buddy_voice.output; empty for app streams
    bool corked = false;    // paused
};

// Throw std::runtime_error on output that isn't the expected JSON array.
std::vector<Sink> parse_sinks(std::string_view json);
std::vector<Stream> parse_streams(std::string_view json);

struct BtDevice {
    std::string mac;
    std::string name;  // the alias, as `bluetoothctl devices` prints it
};

// "Device <mac> <name>" lines; anything else (colour codes, prompts, [CHG] lines) is skipped.
std::vector<BtDevice> parse_devices(std::string_view out);

struct BtInfo {
    std::string alias;
    bool connected = false;
    bool audio_sink = false;  // offers A2DP sink: a speaker
};

BtInfo parse_info(std::string_view out);

} // namespace buddy::speakers

// Decides where Buddy's sound goes (D20). Pure logic: main.cpp feeds it what PipeWire shows (World) and the MQTT
// inputs, runs the Actions it returns with pactl/pw-link/bluetoothctl, and calls step() again on the next change.
//
//   music (Soloist → buddy_music) plays  → connect the selected Nest, link buddy_music.output to it
//   no music for `idle`                  → disconnect it (Bluetooth only while needed)
//   voice (buddy_voice)                  → that Nest while music plays there (at voice_percent), else the default sink
//                                          (Jabra)
//   ears listening or voice speaking     → buddy_music down to duck_percent, back after duck_hold of quiet
//
// Only one Nest at a time: the Pi's Bluetooth refuses a second A2DP connection (#16).
#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace buddy::speakers {

using Clock = std::chrono::steady_clock;

struct Speaker {
    std::string id;  // "bt:<mac>"
    std::string name;
    std::string mac;
};

std::string speaker_id(const std::string& mac);

struct RouterConfig {
    std::chrono::seconds idle{600};
    int duck_percent = 30;
    std::chrono::milliseconds duck_hold{1500};
    int volume = 80;  // the Nest's own volume, set once per connection
    // after connecting: the Nest syncs its own volume first and overwrites one set earlier
    std::chrono::milliseconds volume_delay{3000};
    int voice_percent = 60;  // Buddy's voice on a Nest, % (on the Jabra: 100)
    // A shorter gap isn't a pause: Soloist briefly pauses on "next" and on transfers
    std::chrono::milliseconds music_hold{2000};
};

// What PipeWire shows right now
struct World {
    struct Nest {
        std::string mac;
        std::string sink;  // bluez_output.…
    };
    std::vector<Nest> connected;  // Nests with a Bluetooth sink
    bool music = false;           // something plays into buddy_music (Soloist), not paused
};

struct Action {
    enum class Kind {
        Connect,     // target: MAC
        Disconnect,  // target: MAC
        LinkMusic,   // target: sink: link buddy_music.output to it
        MoveVoice,   // target: sink, "" = the default sink
        SetVolume,   // target: sink (a Nest, or buddy_music for ducking), percent
    };
    Kind kind;
    std::string target;
    int percent = 0;

    bool operator==(const Action&) const = default;
};

class Router {
public:
    Router(std::vector<Speaker> speakers, RouterConfig cfg, const std::string& default_speaker);

    const std::vector<Speaker>& speakers() const { return speakers_; }
    const Speaker* selected() const;
    const Speaker* find(const std::string& mac) const;

    // Commands. Return "" when done, else a reply to speak ("Which speaker, …?").
    std::string select(const std::string& id_or_name);  // the music's Nest from now on
    std::string connect(const std::string& id_or_name, Clock::time_point now);  // select + connect without music
    void disconnect();  // and stay off until music starts again

    void set_listening(bool on) { listening_ = on; }
    void set_speaking(bool on) { speaking_ = on; }
    // A Connect action failed: no retry until the music starts again or another Nest is selected.
    void connect_failed(const std::string& mac) { failed_ = mac; }

    std::vector<Action> step(const World& world, Clock::time_point now);
    // Next time step() has timed work (restore after a duck, idle disconnect); nullopt = only on events
    std::optional<Clock::time_point> deadline() const;

    nlohmann::json state() const;

private:
    // id, else the best name match; nullptr + reply in `error`
    const Speaker* resolve(const std::string& id_or_name, std::string& error) const;

    std::vector<Speaker> speakers_;
    RouterConfig cfg_;
    std::string selected_;  // id

    World world_;
    bool music_ = false;                            // playing, or paused for less than music_hold
    std::optional<Clock::time_point> paused_at_;    // Soloist's stream stopped, music_ still true
    std::optional<Clock::time_point> last_music_;  // also set by connect(): counts as use
    bool held_off_ = false;                        // after disconnect(), until the music starts again
    std::string failed_;                           // MAC that didn't connect
    std::map<std::string, Clock::time_point> connected_at_;  // MAC → first seen connected
    std::set<std::string> volume_set_;                       // MACs whose volume was set since they connected
    std::string music_sink_;                       // where buddy_music.output is linked
    std::optional<std::string> voice_sink_;        // where buddy_voice.output goes ("" = default); nullopt = unknown

    bool listening_ = false;
    bool speaking_ = false;
    bool ducked_ = true;  // unknown at start: restore 100 % on the first step
    std::optional<Clock::time_point> quiet_since_ = Clock::time_point{};
};

} // namespace buddy::speakers

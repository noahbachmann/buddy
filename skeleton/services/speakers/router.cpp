#include "router.hpp"

#include <algorithm>

#include "buddy/names.hpp"

namespace buddy::speakers {

std::string speaker_id(const std::string& mac) { return "bt:" + mac; }

Router::Router(std::vector<Speaker> speakers, RouterConfig cfg, const std::string& default_speaker)
    : speakers_(std::move(speakers)), cfg_(cfg) {
    std::string error;
    if (const Speaker* s = default_speaker.empty() ? nullptr : resolve(default_speaker, error)) selected_ = s->id;
    else if (!speakers_.empty()) selected_ = speakers_.front().id;
}

const Speaker* Router::selected() const {
    for (const auto& s : speakers_)
        if (s.id == selected_) return &s;
    return nullptr;
}

const Speaker* Router::find(const std::string& mac) const {
    for (const auto& s : speakers_)
        if (s.mac == mac) return &s;
    return nullptr;
}

const Speaker* Router::resolve(const std::string& id_or_name, std::string& error) const {
    for (const auto& s : speakers_)
        if (s.id == id_or_name) return &s;
    std::vector<std::string> names;
    for (const auto& s : speakers_) names.push_back(s.name);
    const auto hits = names::best_matches(names, id_or_name);
    if (hits.size() == 1) return &speakers_[hits.front()];
    if (hits.empty()) error = "I don't know a speaker called " + id_or_name + ".";
    else error = "Which speaker, " + names[hits[0]] + " or " + names[hits[1]] + "?";
    return nullptr;
}

std::string Router::select(const std::string& id_or_name) {
    std::string error;
    const Speaker* s = resolve(id_or_name, error);
    if (!s) return error;
    if (s->id != selected_) failed_.clear();
    selected_ = s->id;
    return {};
}

std::string Router::connect(const std::string& id_or_name, Clock::time_point now) {
    if (auto error = select(id_or_name); !error.empty()) return error;
    failed_.clear();
    held_off_ = false;
    last_music_ = now;
    return {};
}

void Router::disconnect() {
    held_off_ = true;
    last_music_.reset();
}

std::vector<Action> Router::step(const World& world, Clock::time_point now) {
    using K = Action::Kind;
    std::vector<Action> out;
    world_ = world;
    auto present = [&](const std::string& sink) {
        return std::ranges::any_of(world.connected, [&](const World::Nest& n) { return n.sink == sink; });
    };
    // Forget links to Nests that went away (the links went with them; WirePlumber sends voice back to the default)
    if (!music_sink_.empty() && !present(music_sink_)) music_sink_.clear();
    if (voice_sink_ && !voice_sink_->empty() && !present(*voice_sink_)) voice_sink_ = "";
    auto gone = [&](const std::string& mac) {
        return std::ranges::none_of(world.connected, [&](const World::Nest& n) { return n.mac == mac; });
    };
    std::erase_if(volume_set_, gone);
    std::erase_if(connected_at_, [&](const auto& kv) { return gone(kv.first); });
    for (const auto& n : world.connected) connected_at_.try_emplace(n.mac, now);

    if (world.music && !music_) {  // music (re)starts: a failed Nest gets another try, a disconnect is over
        failed_.clear();
        held_off_ = false;
    }
    if (world.music || music_) last_music_ = now;  // idle counts from when the music stopped
    music_ = world.music;
    const bool active = !held_off_ && last_music_ && now - *last_music_ < cfg_.idle;

    const Speaker* sel = selected();
    const World::Nest* nest = nullptr;
    for (const auto& n : world.connected) {
        if (sel && n.mac == sel->mac) nest = &n;
        else out.push_back({K::Disconnect, n.mac});  // one Nest at a time
    }
    if (sel && active && !nest && failed_ != sel->mac) out.push_back({K::Connect, sel->mac});
    if (nest && !active) out.push_back({K::Disconnect, nest->mac});
    if (nest && active) {
        if (!volume_set_.contains(nest->mac) && now - connected_at_[nest->mac] >= cfg_.volume_delay) {
            out.push_back({K::SetVolume, nest->sink, cfg_.volume});
            volume_set_.insert(nest->mac);
        }
        if (music_sink_ != nest->sink) {
            out.push_back({K::LinkMusic, nest->sink});
            music_sink_ = nest->sink;
        }
    }

    // Voice follows the music to the Nest; never moved mid-sentence
    const std::string voice = nest && active && music_ ? nest->sink : "";
    if (!speaking_ && voice_sink_ != voice) {
        out.push_back({K::MoveVoice, voice});
        voice_sink_ = voice;
    }

    if (listening_ || speaking_) {
        quiet_since_.reset();
        if (!ducked_) {
            out.push_back({K::SetVolume, "buddy_music", cfg_.duck_percent});
            ducked_ = true;
        }
    } else if (ducked_) {
        if (!quiet_since_) quiet_since_ = now;
        if (now - *quiet_since_ >= cfg_.duck_hold) {
            out.push_back({K::SetVolume, "buddy_music", 100});
            ducked_ = false;
            quiet_since_.reset();
        }
    }
    return out;
}

std::optional<Clock::time_point> Router::deadline() const {
    std::optional<Clock::time_point> next;
    auto earliest = [&](Clock::time_point t) {
        if (!next || t < *next) next = t;
    };
    if (ducked_ && quiet_since_ && !listening_ && !speaking_) earliest(*quiet_since_ + cfg_.duck_hold);
    if (!music_ && last_music_ && !world_.connected.empty()) earliest(*last_music_ + cfg_.idle);
    for (const auto& [mac, at] : connected_at_)
        if (!volume_set_.contains(mac)) earliest(at + cfg_.volume_delay);
    return next;
}

nlohmann::json Router::state() const {
    auto list = nlohmann::json::array();
    std::string voice = "jabra";
    for (const auto& s : speakers_) {
        const auto it = std::ranges::find_if(world_.connected, [&](const World::Nest& n) { return n.mac == s.mac; });
        const bool connected = it != world_.connected.end();
        list.push_back({{"id", s.id}, {"name", s.name}, {"connected", connected}});
        if (connected && voice_sink_ && *voice_sink_ == it->sink) voice = s.id;
    }
    return {{"speakers", list},
            {"selected", selected_.empty() ? nlohmann::json() : nlohmann::json(selected_)},
            {"music", music_ ? "playing" : "idle"},
            {"voice", voice},
            {"ducked", ducked_}};
}

} // namespace buddy::speakers

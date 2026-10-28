#include "home.hpp"

#include <algorithm>
#include <cmath>

#include "buddy/names.hpp"

namespace buddy::hue {

namespace {

using json = nlohmann::json;

// d[key] for a const json: its operator[] asserts on a missing key instead of returning null
const json& field(const json& d, const char* key) {
    static const json null;
    if (!d.is_object()) return null;
    const auto it = d.find(key);
    return it == d.end() ? null : *it;
}

std::string name_of(const json& r) {
    const auto& name = field(field(r, "metadata"), "name");
    return name.is_string() ? name.get<std::string>() : "";
}

// rids of `list` (children / services) with rtype `rtype`
std::vector<std::string> rids(const json& list, const char* rtype) {
    std::vector<std::string> out;
    if (!list.is_array()) return out;
    for (const auto& ref : list)
        if (field(ref, "rtype") == rtype && field(ref, "rid").is_string()) out.push_back(ref["rid"]);
    return out;
}

// on / dimming / color_temperature / color fields of a light or grouped_light, present ones only
template <typename T>
void read_state(const json& r, T& t) {
    if (const auto& on = field(field(r, "on"), "on"); on.is_boolean()) t.on = on;
    if (const auto& b = field(field(r, "dimming"), "brightness"); b.is_number()) t.brightness = b;
}

void read_color(const json& r, Light& l) {
    if (const auto& ct = field(r, "color_temperature"); ct.is_object()) {
        const auto& m = field(ct, "mirek");
        const auto& valid = field(ct, "mirek_valid");
        if (m.is_number() && !(valid.is_boolean() && !valid.get<bool>())) l.mirek = m.get<int>();
        else if (m.is_null() || (valid.is_boolean() && !valid.get<bool>())) l.mirek.reset();
    }
    if (const auto& xy = field(field(r, "color"), "xy"); xy.is_object() && field(xy, "x").is_number())
        l.xy = {{xy["x"].get<double>(), field(xy, "y").is_number() ? xy["y"].get<double>() : 0.0}};
}

int percent(double brightness) { return static_cast<int>(std::lround(brightness)); }

bool home_word(const std::string& s) {
    static const std::vector<std::string> words = {"all", "everything", "everywhere", "house", "the house", "home",
                                                   "whole house", "the whole house", "all lights", "all the lights"};
    return std::find(words.begin(), words.end(), names::lower(s)) != words.end();
}

} // namespace

void Home::load(const json& resources) {
    lights_.clear();
    groups_.clear();
    group_of_service_.clear();
    scenes_.clear();

    std::map<std::string, std::vector<std::string>> device_lights;  // device id → light ids
    std::map<std::string, const json*> grouped;                     // grouped_light id → resource
    for (const auto& r : resources) {
        const auto& type = field(r, "type");
        if (!field(r, "id").is_string()) continue;
        const std::string id = r["id"];
        if (type == "device") {
            device_lights[id] = rids(field(r, "services"), "light");
        } else if (type == "light") {
            Light l{.id = id, .name = name_of(r)};
            read_state(r, l);
            read_color(r, l);
            l.dimming = field(r, "dimming").is_object();
            l.temperature = field(r, "color_temperature").is_object();
            l.color = field(r, "color").is_object();
            lights_[id] = std::move(l);
        } else if (type == "grouped_light") {
            grouped[id] = &r;
        }
    }
    for (const auto& r : resources) {
        const auto& type = field(r, "type");
        if (type != "room" && type != "zone" && type != "bridge_home") continue;
        if (!field(r, "id").is_string()) continue;
        Group g{.id = r["id"], .name = type == "bridge_home" ? "All lights" : name_of(r), .kind = type == "bridge_home" ? "home" : type.get<std::string>()};
        if (const auto services = rids(field(r, "services"), "grouped_light"); !services.empty()) g.grouped_light = services[0];
        if (g.grouped_light.empty()) continue;  // can't switch it (room without lights)
        if (const auto it = grouped.find(g.grouped_light); it != grouped.end()) read_state(*it->second, g);
        if (g.kind == "home") {
            for (const auto& [lid, l] : lights_) g.lights.push_back(lid);
        } else {
            for (const auto& dev : rids(field(r, "children"), "device"))
                for (const auto& lid : device_lights[dev]) g.lights.push_back(lid);
            for (const auto& lid : rids(field(r, "children"), "light")) g.lights.push_back(lid);
        }
        std::erase_if(g.lights, [&](const std::string& lid) { return !lights_.contains(lid); });
        if (g.kind == "room")
            for (const auto& lid : g.lights) lights_[lid].room = g.id;
        group_of_service_[g.grouped_light] = g.id;
        groups_[g.id] = std::move(g);
    }
    for (const auto& r : resources) {
        if (field(r, "type") != "scene" || !field(r, "id").is_string()) continue;
        const auto& group = field(field(r, "group"), "rid");
        scenes_[r["id"]] = {.id = r["id"], .name = name_of(r), .group = group.is_string() ? group.get<std::string>() : ""};
    }
    loaded_ = true;
}

bool Home::apply(const json& events) {
    if (!events.is_array()) return false;
    bool reload = false;
    for (const auto& event : events) {
        const auto& kind = field(event, "type");
        const auto& data = field(event, "data");
        if (!data.is_array()) continue;
        for (const auto& r : data) {
            const auto& type = field(r, "type");
            const auto& id_json = field(r, "id");
            if (!id_json.is_string()) continue;
            const std::string id = id_json;
            if (kind == "add" || kind == "delete") {
                if (type == "light" || type == "grouped_light" || type == "room" || type == "zone" || type == "scene" ||
                    type == "device")
                    reload = true;
                continue;
            }
            if (kind != "update") continue;
            if (type == "light") {
                const auto it = lights_.find(id);
                if (it == lights_.end()) {
                    reload = true;
                    continue;
                }
                read_state(r, it->second);
                read_color(r, it->second);
                if (field(r, "metadata").is_object()) reload = true;  // renamed
            } else if (type == "grouped_light") {
                if (const auto g = group_of_service_.find(id); g != group_of_service_.end()) read_state(r, groups_[g->second]);
            } else if (type == "room" || type == "zone" || type == "scene" || type == "device") {
                // scene recalls only change "status"; names and members need a reload
                if (field(r, "metadata").is_object() || field(r, "children").is_array() || field(r, "services").is_array() ||
                    field(r, "group").is_object())
                    reload = true;
            }
        }
    }
    return reload;
}

const Group* Home::home() const {
    for (const auto& [id, g] : groups_)
        if (g.kind == "home") return &g;
    return nullptr;
}

const Light* Home::light(const std::string& id) const {
    const auto it = lights_.find(id);
    return it == lights_.end() ? nullptr : &it->second;
}

std::optional<Target> Home::find(const std::string& spoken) const {
    if (home_word(spoken)) {
        if (const Group* h = home()) return Target{.group = h};
        return std::nullopt;
    }
    std::vector<Target> targets;
    std::vector<std::string> target_names;
    for (const auto& [id, g] : groups_) {
        if (g.kind == "home") continue;
        targets.push_back({.group = &g});
        target_names.push_back(g.name);
    }
    for (const auto& [id, l] : lights_) {
        targets.push_back({.light = &l});
        target_names.push_back(l.name);
    }
    auto best = names::best_matches(target_names, spoken);
    if (best.empty()) return std::nullopt;
    // equal scores: rooms, then zones, then lights
    for (const char* kind : {"room", "zone"}) {
        std::vector<std::size_t> of_kind;
        for (const auto i : best)
            if (targets[i].group && targets[i].group->kind == kind) of_kind.push_back(i);
        if (!of_kind.empty()) {
            best = of_kind;
            break;
        }
    }
    if (best.size() > 1) throw Ambiguous(target_names[best[0]], target_names[best[1]]);
    return targets[best[0]];
}

std::optional<Target> Home::find_id(const std::string& id) const {
    if (const auto g = groups_.find(id); g != groups_.end()) return Target{.group = &g->second};
    if (const auto s = group_of_service_.find(id); s != group_of_service_.end()) return Target{.group = &groups_.at(s->second)};
    if (const auto l = lights_.find(id); l != lights_.end()) return Target{.light = &l->second};
    return std::nullopt;
}

const Scene* Home::scene(const std::string& spoken, const Group* group) const {
    std::vector<const Scene*> candidates;
    std::vector<std::string> scene_names;
    for (const auto& [id, s] : scenes_) {
        if (group && group->kind != "home" && s.group != group->id) continue;
        candidates.push_back(&s);
        scene_names.push_back(s.name);
    }
    const auto best = names::best_matches(scene_names, spoken);
    if (best.empty()) return nullptr;
    if (best.size() > 1) {
        // same scene name in two rooms ("Relax"): ambiguous only when asked without a room
        auto group_name = [&](const Scene* s) {
            const auto g = groups_.find(s->group);
            return g == groups_.end() ? s->name : s->name + " in " + g->second.name;
        };
        throw Ambiguous(group_name(candidates[best[0]]), group_name(candidates[best[1]]));
    }
    return candidates[best[0]];
}

json Home::state() const {
    json rooms = json::array();
    std::vector<const Group*> groups;
    for (const auto& [id, g] : groups_) groups.push_back(&g);
    // home first, then rooms and zones by name
    std::sort(groups.begin(), groups.end(), [](const Group* a, const Group* b) {
        if ((a->kind == "home") != (b->kind == "home")) return a->kind == "home";
        if (a->kind != b->kind) return a->kind < b->kind;  // room < zone
        return names::lower(a->name) < names::lower(b->name);
    });
    for (const Group* g : groups) {
        json scenes = json::array();
        for (const auto& [sid, s] : scenes_)
            if (s.group == g->id) scenes.push_back({{"id", s.id}, {"name", s.name}});
        rooms.push_back({{"id", g->id}, {"name", g->name}, {"kind", g->kind}, {"on", g->on},
                         {"brightness", percent(g->brightness)}, {"lights", g->lights}, {"scenes", scenes}});
    }
    json lights = json::array();
    std::vector<const Light*> sorted;
    for (const auto& [id, l] : lights_) sorted.push_back(&l);
    std::sort(sorted.begin(), sorted.end(), [](const Light* a, const Light* b) { return names::lower(a->name) < names::lower(b->name); });
    for (const Light* l : sorted) {
        json j = {{"id", l->id}, {"name", l->name}, {"room", l->room}, {"on", l->on}, {"brightness", percent(l->brightness)},
                  {"color", l->color}, {"temperature", l->temperature}, {"dimming", l->dimming}};
        if (l->mirek) j["mirek"] = *l->mirek;
        if (l->xy) j["xy"] = {l->xy->first, l->xy->second};
        lights.push_back(std::move(j));
    }
    return {{"rooms", rooms}, {"lights", lights}};
}

} // namespace buddy::hue

#pragma once
// What the bridge knows, flattened for commands and the web panel: rooms and zones (each with the grouped_light
// that switches them), lights, scenes. Built from GET /clip/v2/resource, kept current by eventstream updates.
//
// CLIP v2 links: room → children devices → their light services; zone → children lights (or devices);
// room/zone/bridge_home → services grouped_light; scene → group (room or zone).

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace buddy::hue {

struct Light {
    std::string id{}, name{};
    std::string room{};  // room id, empty = in no room
    bool on = false;
    double brightness = 0;       // 0..100
    std::optional<int> mirek{};  // colour temperature, unset while in colour (xy) mode
    std::optional<std::pair<double, double>> xy{};
    bool color = false, temperature = false, dimming = false;  // what the light can do
};

struct Group {
    std::string id{};             // room / zone / bridge_home id
    std::string grouped_light{};  // the service that switches all its lights
    std::string name{};
    std::string kind{};  // room, zone, home
    bool on = false;
    double brightness = 0;
    std::vector<std::string> lights{};  // light ids
};

struct Scene {
    std::string id, name;
    std::string group;  // room / zone id
};

// What a command acts on: a group (via its grouped_light) or one light.
struct Target {
    const Group* group = nullptr;
    const Light* light = nullptr;
    const std::string& name() const { return group ? group->name : light->name; }
};

// The spoken name fits more than one room/light equally well.
struct Ambiguous : std::runtime_error {
    Ambiguous(std::string a, std::string b) : std::runtime_error(a + " / " + b), a(std::move(a)), b(std::move(b)) {}
    std::string a, b;
};

class Home {
public:
    // Replaces everything with the bridge's resource list.
    void load(const nlohmann::json& resources);
    bool loaded() const { return loaded_; }

    // Applies an eventstream message (array of {type, data: [...]}). Returns true when something changed that
    // needs a full reload (rooms, scenes, lights added, removed or renamed).
    bool apply(const nlohmann::json& events);

    // Spoken name → target. Rooms win over zones over lights on equal scores ("bedroom" = the room, not
    // "Bedroom lamp"); "all", "everything", "house", … = every light. nullopt = no match. Throws Ambiguous.
    std::optional<Target> find(const std::string& spoken) const;
    // Room/zone/home id, grouped_light id or light id (web panel).
    std::optional<Target> find_id(const std::string& id) const;
    const Group* home() const;  // every light; nullptr before load
    const Light* light(const std::string& id) const;

    // Best-matching scene name in `group` (any group if null). nullptr = none. Throws Ambiguous.
    const Scene* scene(const std::string& spoken, const Group* group) const;

    // Retained buddy/hue/state payload (without "bridge").
    nlohmann::json state() const;

private:
    bool loaded_ = false;
    std::map<std::string, Light> lights_;  // by id
    std::map<std::string, Group> groups_;  // by room / zone / bridge_home id
    std::map<std::string, std::string> group_of_service_;  // grouped_light id → group id
    std::map<std::string, Scene> scenes_;
};

} // namespace buddy::hue

#include "lights.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>

#include "buddy/names.hpp"

namespace buddy::hue {

namespace {

using json = nlohmann::json;

Plan failed(std::string say) { return {.ok = false, .say = std::move(say), .requests = {}}; }

Plan said(std::string say) { return {.ok = true, .say = std::move(say), .requests = {}}; }

Request request(const Target& t, json body) {
    if (t.group) return {.type = "grouped_light", .id = t.group->grouped_light, .body = std::move(body)};
    return {.type = "light", .id = t.light->id, .body = std::move(body)};
}

Plan change(const Target& t, json body) { return {.ok = true, .say = "", .requests = {request(t, std::move(body))}}; }

// "lights", "the lights" for groups (rooms are named "Kitchen", not "Kitchen lights"), the light's own name otherwise
std::string spoken(const Target& t) {
    if (t.light) return "the " + t.light->name;
    if (t.group->kind == "home") return "the lights";
    return "the " + t.group->name + " lights";
}

// What the target's lights can do: any light in a group is enough (the bridge skips the others).
bool can(const Target& t, const Home& home, bool Light::*capability) {
    if (t.light) return t.light->*capability;
    return std::any_of(t.group->lights.begin(), t.group->lights.end(), [&](const std::string& id) {
        const Light* l = home.light(id);
        return l && l->*capability;
    });
}

std::string capitalized(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::optional<int> parse_int(const std::string& s) {
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || end != s.data() + s.size()) return std::nullopt;
    return v;
}

Plan status(const Target& t) {
    const bool on = t.group ? t.group->on : t.light->on;
    const int pct = static_cast<int>(std::lround(t.group ? t.group->brightness : t.light->brightness));
    const std::string verb = t.light ? " is " : " are ";
    if (!on) return said(capitalized(spoken(t)) + verb + "off.");
    return said(capitalized(spoken(t)) + verb + "on at " + std::to_string(pct) + " percent.");
}

} // namespace

std::optional<std::pair<double, double>> color_xy(const std::string& name) {
    // Gamut C corners for the saturated ones; the bridge clamps to each light's gamut
    static const std::map<std::string, std::pair<double, double>> colors = {
        {"red", {0.675, 0.322}},     {"orange", {0.556, 0.408}}, {"yellow", {0.443, 0.515}},
        {"green", {0.17, 0.7}},      {"cyan", {0.17, 0.34}},     {"turquoise", {0.17, 0.34}},
        {"blue", {0.153, 0.048}},    {"purple", {0.25, 0.1}},    {"violet", {0.25, 0.1}},
        {"magenta", {0.385, 0.155}}, {"pink", {0.45, 0.25}},
    };
    const auto it = colors.find(names::lower(name));
    if (it == colors.end()) return std::nullopt;
    return it->second;
}

std::optional<int> white_mirek(const std::string& name) {
    std::string n = names::lower(name);
    if (n.ends_with(" kelvin")) n.resize(n.size() - 7);
    if (n.ends_with("k")) n.pop_back();
    if (const auto kelvin = parse_int(n)) {
        if (*kelvin < 1000 || *kelvin > 10000) return std::nullopt;
        return std::clamp(static_cast<int>(std::lround(1e6 / *kelvin)), 153, 500);
    }
    static const std::map<std::string, int> whites = {
        {"candle", 454},     {"candlelight", 454}, {"extra warm", 454}, {"extra warm white", 454},
        {"warm", 370},       {"warm white", 370},  {"soft white", 370}, {"white", 250},
        {"neutral", 250},    {"neutral white", 250}, {"natural", 250},  {"natural white", 250},
        {"cool", 200},       {"cool white", 200},  {"cold", 200},       {"cold white", 200},
        {"bright white", 200}, {"daylight", 153},
    };
    const auto it = whites.find(n);
    if (it == whites.end()) return std::nullopt;
    return it->second;
}

Plan plan(const Command& c, const Home& home, const LightsConfig& cfg) {
    if (c.action.empty()) return {};
    if (!home.loaded()) return failed("I can't reach the Hue bridge.");

    std::optional<Target> target;
    const bool named = !c.id.empty() || !c.device.empty();
    try {
        if (!c.id.empty()) {
            target = home.find_id(c.id);
            if (!target) return failed("That light is gone.");
        } else if (!c.device.empty()) {
            target = home.find(c.device);
            if (!target) return failed("I don't know a room or light called " + c.device + ".");
        } else if (!cfg.default_room.empty()) {
            target = home.find(cfg.default_room);
            if (!target) return failed("I can't find the room " + cfg.default_room + ".");
        } else if (const Group* all = home.home()) {
            target = Target{.group = all};
        } else {
            return failed("The Hue bridge has no lights.");
        }
    } catch (const Ambiguous& e) {
        return failed("Which one, " + e.a + " or " + e.b + "?");
    }
    const Target& t = *target;
    const json turn_on = {{"on", true}};

    if (c.action == "on") return change(t, {{"on", turn_on}});
    if (c.action == "off") return change(t, {{"on", {{"on", false}}}});
    if (c.action == "toggle") return change(t, {{"on", {{"on", !(t.group ? t.group->on : t.light->on)}}}});
    if (c.action == "status") return status(t);

    if (c.action == "brightness" || c.action == "brighter" || c.action == "darker") {
        if (!can(t, home, &Light::dimming)) return failed(capitalized(spoken(t)) + " can't dim.");
        if (c.action == "brighter")
            return change(t, {{"on", turn_on}, {"dimming_delta", {{"action", "up"}, {"brightness_delta", cfg.step}}}});
        if (c.action == "darker")
            return change(t, {{"dimming_delta", {{"action", "down"}, {"brightness_delta", cfg.step}}}});
        const auto pct = parse_int(c.query);
        if (!pct || *pct < 0 || *pct > 100) return failed("Brightness goes from 0 to 100 percent.");
        if (*pct == 0) return change(t, {{"on", {{"on", false}}}});
        return change(t, {{"on", turn_on}, {"dimming", {{"brightness", *pct}}}});
    }

    if (c.action == "color" || c.action == "temperature") {
        if (const auto mirek = white_mirek(c.query)) {
            if (!can(t, home, &Light::temperature)) return failed(capitalized(spoken(t)) + " can't change their white.");
            return change(t, {{"on", turn_on}, {"color_temperature", {{"mirek", *mirek}}}});
        }
        const auto xy = c.action == "color" ? color_xy(c.query) : std::nullopt;
        if (!xy) return failed("I don't know the colour " + c.query + ".");
        if (!can(t, home, &Light::color)) return failed(capitalized(spoken(t)) + " can't change colour.");
        return change(t, {{"on", turn_on}, {"color", {{"xy", {{"x", xy->first}, {"y", xy->second}}}}}});
    }

    if (c.action == "scene") {
        if (c.query.empty()) return failed("Which scene?");
        try {
            // a named room: its scenes only. Else the default room's first, then every room's.
            const Group* in = t.group && t.group->kind != "home" ? t.group : nullptr;
            const Scene* s = home.scene(c.query, in);
            if (!s && !named && in) s = home.scene(c.query, nullptr);
            if (!s) return failed("I don't know a scene called " + c.query + (named && in ? " in " + in->name : "") + ".");
            return {.ok = true, .say = "", .requests = {{.type = "scene", .id = s->id, .body = {{"recall", {{"action", "active"}}}}}}};
        } catch (const Ambiguous& e) {
            return failed("Which one, " + e.a + " or " + e.b + "?");
        }
    }

    return {.ok = false, .say = "", .requests = {}};  // unknown action: logged, not spoken
}

} // namespace buddy::hue

#include "fleet.hpp"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

namespace buddy::messenger {

namespace {

using json = nlohmann::json;

// 7.0 → "7", 7.9 → "7.9": the line stays short on round values
std::string trimmed(double v) {
    const double rounded = std::round(v * 10) / 10;
    std::string s = std::to_string(rounded);
    s.erase(s.find('.') + 2);
    if (s.ends_with(".0")) s.erase(s.size() - 2);
    return s;
}

// The payload's optionals: absent fields leave their part out of the line.
std::string line_of(const json& j) {
    std::string out;
    auto add = [&](const std::string& part) {
        if (!out.empty()) out += " · ";
        out += part;
    };
    if (const auto cpu = j.find("cpu"); cpu != j.end() && cpu->contains("pct")) add("CPU " + std::to_string(cpu->at("pct").get<int>()) + "%");
    if (const auto t = j.find("temp_c"); t != j.end()) add(trimmed(t->get<double>()) + "°");
    if (const auto m = j.find("mem"); m != j.end())
        add("RAM " + trimmed(m->at("used_mb").get<double>() / 1024) + "/" + trimmed(m->at("total_mb").get<double>() / 1024) + "G");
    if (const auto d = j.find("disk"); d != j.end())
        add("💾 " + std::to_string(std::lround(d->at("used_gb").get<double>())) + "/" +
            std::to_string(std::lround(d->at("total_gb").get<double>())) + "G");
    if (const auto t = j.find("throttled"); t != j.end()) {
        std::string flags;
        for (const auto& f : t->at("now")) flags += (flags.empty() ? "" : ", ") + f.get<std::string>();
        if (!flags.empty()) add("⚠ " + flags);
    }
    return out;
}

bool is_system(const std::string& service) {
    return service == "system" || service.starts_with("system-");
}

} // namespace

void Fleet::state(const std::string& service, const std::string& payload) {
    if (!is_system(service)) return;
    json j = json::parse(payload, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("label") || !j["label"].is_string()) return;
    std::string line;
    try {
        line = line_of(j);
    } catch (const json::exception&) {
        return;  // a field with the wrong shape: keep the previous line
    }
    auto it = std::ranges::find(hosts_, service, &Host::service);
    if (it == hosts_.end()) it = hosts_.insert(hosts_.end(), Host{.service = service, .label = "", .line = "", .online = true});
    it->label = j["label"].get<std::string>();
    it->line = std::move(line);
}

void Fleet::status(const std::string& service, bool online) {
    const auto it = std::ranges::find(hosts_, service, &Host::service);
    if (it != hosts_.end()) it->online = online;
}

std::string Fleet::summary() const {
    std::vector<const Host*> sorted;
    for (const auto& h : hosts_) sorted.push_back(&h);
    std::ranges::sort(sorted, {}, [](const Host* h) { return h->label; });
    std::string out;
    for (const Host* h : sorted) {
        if (!out.empty()) out += "\n";
        if (!h->online) out += "🔴 " + h->label + " · offline";
        else out += "🟢 " + h->label + (h->line.empty() ? "" : " · " + h->line);
    }
    return out;
}

} // namespace buddy::messenger

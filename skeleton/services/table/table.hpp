#pragma once
// buddy/table/cmd → tracker → buddy/table/event: one command, its events (hand_corrected first, then exactly one
// done or failed for its seq). No MQTT, no backend of its own.

#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "tracker.hpp"

namespace buddy::table {

class Table {
public:
    Table(Backend& backend, TrackerConfig cfg) : backend_(backend), tracker_(backend, cfg) {}

    std::vector<Event> execute(const Command& c);
    // buddy/table/state (debug only, never on the panel): the belief.
    nlohmann::json state() const;

    const Tracker& tracker() const { return tracker_; }

private:
    Backend& backend_;
    Tracker tracker_;
};

} // namespace buddy::table

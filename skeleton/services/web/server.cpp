#include "server.hpp"

#include <memory>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "access.hpp"

namespace buddy::web {

namespace {

using json = nlohmann::json;

json payload_json(const std::string& payload) {
    if (payload.empty()) return nullptr;
    auto j = json::parse(payload, nullptr, /*allow_exceptions=*/false);
    return j.is_discarded() ? json(payload) : j;
}

void error(httplib::Response& res, int status, const std::string& why) {
    res.status = status;
    res.set_content(json{{"error", why}}.dump(), "application/json");
}

} // namespace

bool add_routes(httplib::Server& server, StateStore& store, Publish publish, const WebConfig& cfg) {
    server.set_pre_routing_handler([cfg](const httplib::Request& req, httplib::Response& res) {
        if (!allowed_host(req.get_header_value("Host"), cfg.extra_hosts)) {
            error(res, 403, "unknown host name: open the panel as http://buddy.local or by IP");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    server.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");  // small files, LAN: always the deployed version
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("X-Frame-Options", "DENY");
    });
    server.set_payload_max_length(64 * 1024);

    server.Get("/api/state", [&store](const httplib::Request&, httplib::Response& res) {
        std::uint64_t since = 0;
        json state = json::object();
        for (const auto& u : store.wait(since, std::chrono::milliseconds(0))) state[u.topic] = payload_json(u.payload);
        res.set_content(state.dump(), "application/json");
    });

    server.Get("/api/events", [&store, keepalive = cfg.keepalive](const httplib::Request&, httplib::Response& res) {
        auto since = std::make_shared<std::uint64_t>(0);
        res.set_chunked_content_provider("text/event-stream", [&store, since, keepalive](std::size_t, httplib::DataSink& sink) {
            if (store.closed()) return false;
            const auto updates = store.wait(*since, keepalive);
            std::string out;
            for (const auto& u : updates)
                out += "data: " + json{{"topic", u.topic}, {"payload", payload_json(u.payload)}}.dump() + "\n\n";
            if (out.empty()) out = ": keepalive\n\n";
            return !store.closed() && sink.write(out.data(), out.size());  // false: browser gone, frees the thread
        });
    });

    server.Post(R"(/api/cmd/([a-z_]+))", [publish](const httplib::Request& req, httplib::Response& res) {
        if (!req.get_header_value("Content-Type").starts_with("application/json"))
            return error(res, 415, "Content-Type must be application/json");
        if (const auto origin = req.get_header_value("Origin");
            !origin.empty() && !same_origin(origin, req.get_header_value("Host")))
            return error(res, 403, "cross-site request");
        const auto topic = command_topic(req.matches[1].str());
        if (!topic) return error(res, 404, "no such service");
        if (const auto why = command_error(req.body); !why.empty()) return error(res, 400, why);
        publish(*topic, req.body);
        res.status = 202;
        res.set_content(R"({"ok":true})", "application/json");
    });

    return server.set_mount_point("/", cfg.root);
}

} // namespace buddy::web

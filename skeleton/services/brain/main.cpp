// brain: transcript → intent → skill command.
//
//   buddy-brain                  live: buddy/ears/transcript in, buddy/<skill>/cmd + buddy/brain/intent out
//   buddy-brain --test "text"…   offline: prints the intent for each argument (no MQTT)
//
// Routing: an intent's skill names the service; the command goes to buddy/<skill>/cmd as
// {"action": <intent>[, "query": …]}. Not understood → buddy/voice/say "Sorry, …".
#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "buddy/config.hpp"
#include "buddy/lifecycle.hpp"
#include "buddy/mqtt.hpp"
#include "buddy/topics.hpp"
#include "rules.hpp"

namespace {

using namespace buddy;
using namespace buddy::brain;
using json = nlohmann::json;

const char* kNotUnderstood = "Sorry, I didn't catch that.";

std::string_view command_topic(const std::string& skill) {
    if (skill == "spotify") return topic::spotify_cmd;
    return {};
}

json intent_event(const std::optional<Intent>& intent, const std::string& text) {
    json msg = {{"intent", intent ? intent->name : "unknown"}, {"text", text}};
    if (intent) msg["skill"] = intent->skill;
    if (intent && !intent->query.empty()) msg["query"] = intent->query;
    return msg;
}

int test(const IntentParser& parser, int argc, char** argv) {
    for (int i = 2; i < argc; ++i) {
        const auto intent = parser.parse(argv[i]);
        std::cout << "\"" << argv[i] << "\" → " << intent_event(intent, argv[i]).dump() << std::endl;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    install_signal_handlers();
    const RuleParser parser;

    if (argc >= 2 && std::strcmp(argv[1], "--test") == 0) return test(parser, argc, argv);
    if (argc != 1) {
        std::cerr << "usage: " << argv[0] << " [--test \"text\" …]" << std::endl;
        return 2;
    }

    Mqtt mqtt("brain", mqtt_config_from_env());

    mqtt.subscribe(topic::ears_transcript, [&](const std::string&, const std::string& payload) {
        const auto msg = json::parse(payload, nullptr, /*allow_exceptions=*/false);
        if (msg.is_discarded() || !msg.contains("text") || !msg["text"].is_string()) {
            std::cout << "[brain] bad transcript: " << payload << std::endl;
            return;
        }
        const std::string text = msg["text"];
        const auto intent = parser.parse(text);
        const json event = intent_event(intent, text);
        std::cout << "[brain] \"" << text << "\" → " << event["intent"].get<std::string>()
                  << (intent && !intent->query.empty() ? " \"" + intent->query + "\"" : "") << std::endl;
        mqtt.publish(topic::brain_intent, event.dump());

        if (!intent) {
            mqtt.publish(topic::voice_say, json{{"text", kNotUnderstood}}.dump(), 1);
            return;
        }
        const auto cmd_topic = command_topic(intent->skill);
        if (cmd_topic.empty()) {
            std::cout << "[brain] no route for skill " << intent->skill << std::endl;
            return;
        }
        json cmd = {{"action", intent->name}};
        if (!intent->query.empty()) cmd["query"] = intent->query;
        mqtt.publish(cmd_topic, cmd.dump(), 1);
    });

    if (!mqtt.start()) return 0;
    while (running()) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    mqtt.stop();
    return 0;
}

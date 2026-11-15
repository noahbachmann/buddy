#include "telegram.hpp"

#include "store.hpp"

namespace buddy::messenger {

namespace {

using json = nlohmann::json;

constexpr const char* kApi = "https://api.telegram.org/bot";

// result of an API response; throws ApiError
json result_of(long status, const std::string& body) {
    const json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("ok") || !j["ok"].is_boolean()) {  // a proxy's error page, a captive portal
        throw ApiError(status >= 400 ? status : 502, "HTTP " + std::to_string(status) + " without a Telegram answer");
    }
    if (j["ok"].get<bool>()) return j.value("result", json(nullptr));
    const long code = j.contains("error_code") && j["error_code"].is_number_integer() ? j["error_code"].get<long>() : status;
    std::string description = j.contains("description") && j["description"].is_string() ? j["description"].get<std::string>() : "";
    int retry_after = 0;
    if (j.contains("parameters") && j["parameters"].is_object()) {
        const json& p = j["parameters"];
        if (p.contains("retry_after") && p["retry_after"].is_number_integer()) retry_after = p["retry_after"].get<int>();
    }
    throw ApiError(code, description.empty() ? "HTTP " + std::to_string(code) : description, retry_after);
}

std::int64_t integer(const json& j, const char* key) {
    return j.is_object() && j.contains(key) && j[key].is_number_integer() ? j[key].get<std::int64_t>() : 0;
}

std::string text(const json& j, const char* key) {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : "";
}

const json& object(const json& j, const char* key) {
    static const json none = json::object();
    return j.is_object() && j.contains(key) && j[key].is_object() ? j[key] : none;
}

} // namespace

// --- credentials -----------------------------------------------------------------------------------------------------

std::optional<Credentials> load_credentials(const std::string& path) {
    const json j = load_json(path);
    if (j.is_null()) return std::nullopt;
    Credentials c{.token = text(j, "token"), .user_id = integer(j, "user_id"), .chat_id = integer(j, "chat_id")};
    if (c.token.empty() || c.user_id == 0 || c.chat_id == 0) throw std::runtime_error(path + " is incomplete: run --pair again");
    return c;
}

void save_credentials(const std::string& path, const Credentials& c) {
    save_json(path, {{"token", c.token}, {"user_id", c.user_id}, {"chat_id", c.chat_id}});
}

// --- errors ----------------------------------------------------------------------------------------------------------

Failure classify(const ApiError& e) {
    if (e.status == 429 || e.status >= 500) return Failure::Retry;
    if (e.status == 401 || e.status == 404) return Failure::Unauthorized;  // 404: a token of the wrong shape
    if (e.status == 403) return Failure::Blocked;
    if (e.status == 409) return Failure::Conflict;
    return Failure::Refused;
}

// --- updates ---------------------------------------------------------------------------------------------------------

bool Update::carries_code(const std::string& code) const {
    if (!private_chat || from_bot || user_id == 0 || code.empty()) return false;
    const std::string t = trim(in.text);
    return t == code || t == "/start " + code;
}

std::vector<Update> parse_updates(long status, const std::string& body) {
    const json result = result_of(status, body);
    if (!result.is_array()) throw ApiError(502, "getUpdates: no list of updates");
    std::vector<Update> out;
    for (const auto& u : result) {
        Update update;
        update.update_id = integer(u, "update_id");
        if (update.update_id == 0) continue;
        const json* message = nullptr;
        const json* sender = nullptr;
        if (const json& m = object(u, "message"); !m.empty()) {
            message = &m;
            sender = &object(m, "from");
            update.in.text = text(m, "text");
            update.in.reply_to = integer(object(m, "reply_to_message"), "message_id");
        } else if (const json& c = object(u, "callback_query"); !c.empty()) {
            message = &object(c, "message");
            sender = &object(c, "from");
            update.in.callback_id = text(c, "id");
            update.in.data = text(c, "data");
        }
        if (message) {
            const json& chat = object(*message, "chat");
            update.in.message_id = integer(*message, "message_id");
            update.chat_id = integer(chat, "id");
            update.private_chat = text(chat, "type") == "private";
            update.user_id = integer(*sender, "id");
            update.from_bot = sender->value("is_bot", json(false)) == true;
        }
        out.push_back(std::move(update));
    }
    return out;
}

// --- calls -----------------------------------------------------------------------------------------------------------

Telegram::Telegram(std::string token, Http& http) : token_(std::move(token)), http_(http) {}

std::string Telegram::redact(std::string s) const {
    if (token_.empty()) return s;
    for (auto at = s.find(token_); at != std::string::npos; at = s.find(token_, at)) s.replace(at, token_.size(), "<token>");
    return s;
}

json Telegram::call(const std::string& method, const json& params) {
    HttpResponse response;
    try {
        response = http_.request("POST", kApi + token_ + "/" + method, {"Content-Type: application/json"}, params.dump());
    } catch (const HttpError& e) {
        throw HttpError(redact(e.what()));
    }
    return result_of(response.status, response.body);
}

Telegram::Me Telegram::get_me() {
    const json me = call("getMe", json::object());
    return {.id = integer(me, "id"), .username = text(me, "username")};
}

std::int64_t Telegram::send(std::int64_t chat_id, const Send& message) {
    json params{{"chat_id", chat_id}, {"text", message.text}, {"link_preview_options", {{"is_disabled", true}}}};
    if (message.silent) params["disable_notification"] = true;
    if (!message.buttons.empty()) {
        json rows = json::array();
        for (const auto& b : message.buttons) rows.push_back(json::array({{{"text", b.text}, {"callback_data", b.data}}}));
        params["reply_markup"] = {{"inline_keyboard", std::move(rows)}};
    } else if (message.force_reply) {
        params["reply_markup"] = {{"force_reply", true}, {"input_field_placeholder", "Your answer"}};
    }
    const std::int64_t id = integer(call("sendMessage", params), "message_id");
    if (id == 0) throw ApiError(502, "sendMessage: no message_id");
    return id;
}

void Telegram::edit(std::int64_t chat_id, const Edit& edit) {
    try {
        call("editMessageText", {{"chat_id", chat_id},
                                 {"message_id", edit.message_id},
                                 {"text", edit.text},
                                 {"link_preview_options", {{"is_disabled", true}}},
                                 {"reply_markup", {{"inline_keyboard", json::array()}}}});  // buttons off
    } catch (const ApiError& e) {
        // same text again (a resent edit): Telegram calls that an error
        if (e.status != 400 || std::string(e.what()).find("message is not modified") == std::string::npos) throw;
    }
}

void Telegram::answer_callback(const AnswerCallback& answer) {
    json params{{"callback_query_id", answer.callback_id}};
    if (!answer.text.empty()) params["text"] = answer.text;
    call("answerCallbackQuery", params);
}

void Telegram::set_commands() {
    call("setMyCommands", {{"commands",
                            {{{"command", "status"}, {"description", "How Buddy is doing"}},
                             {{"command", "open"}, {"description", "Open questions again"}}}}});
}

std::string Telegram::updates_url(std::int64_t offset, int timeout_s) const {
    std::string url = kApi + token_ + "/getUpdates?timeout=" + std::to_string(timeout_s) +
                      "&allowed_updates=" + url_encode(R"(["message","callback_query"])");
    if (offset != 0) url += "&offset=" + std::to_string(offset);
    return url;
}

} // namespace buddy::messenger

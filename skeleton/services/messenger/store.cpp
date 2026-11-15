#include "store.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace buddy::messenger {

namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::string& what, const std::string& path) {
    throw std::runtime_error(what + " " + path + ": " + std::strerror(errno));
}

} // namespace

json load_json(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        if (!std::filesystem::exists(path)) return nullptr;
        fail("can't read", path);
    }
    json content = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (content.is_discarded()) throw std::runtime_error(path + " is not JSON");
    return content;
}

void save_json(const std::string& path, const json& content) {
    namespace fs = std::filesystem;
    if (const auto dir = fs::path(path).parent_path(); !dir.empty()) fs::create_directories(dir);
    const std::string tmp = path + ".tmp";
    const std::string text = content.dump(2) + "\n";

    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) fail("can't write", tmp);
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            ::close(fd);
            fail("can't write", tmp);
        }
        done += static_cast<std::size_t>(n);
    }
    // an existing tmp keeps its old mode with O_CREAT; power cut after rename must not leave an empty file
    if (::fchmod(fd, 0600) != 0 || ::fsync(fd) != 0) {
        ::close(fd);
        fail("can't write", tmp);
    }
    ::close(fd);
    if (::rename(tmp.c_str(), path.c_str()) != 0) fail("can't replace", path);
}

} // namespace buddy::messenger

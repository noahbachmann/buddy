#include <gtest/gtest.h>

#include <filesystem>

#include "fake_http.hpp"
#include "store.hpp"

using namespace buddy::messenger;
using namespace buddy::test;
namespace fs = std::filesystem;
using nlohmann::json;

TEST(Store, SavesPrivateAndLoadsBack) {
    const auto path = (temp_dir() / "sub" / "messenger.json").string();
    EXPECT_TRUE(load_json(path).is_null());
    save_json(path, {{"token", "T"}, {"chat_id", 42}});
    EXPECT_EQ(load_json(path), (json{{"token", "T"}, {"chat_id", 42}}));
    EXPECT_EQ(fs::status(path).permissions(), fs::perms::owner_read | fs::perms::owner_write);
    EXPECT_FALSE(fs::exists(path + ".tmp"));
}

TEST(Store, ReplacesAndTightensALooseLeftover) {
    const auto dir = temp_dir();
    const auto path = (dir / "q.json").string();
    save_json(path, {{"a", 1}});
    write(path + ".tmp", "leftover of a crash");
    fs::permissions(path + ".tmp", fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read);
    save_json(path, {{"a", 2}});
    EXPECT_EQ(load_json(path)["a"], 2);
    EXPECT_EQ(fs::status(path).permissions(), fs::perms::owner_read | fs::perms::owner_write);
}

TEST(Store, BrokenFileIsAnError) {
    const auto path = (temp_dir() / "q.json").string();
    write(path, "{\"a\": ");
    EXPECT_THROW(load_json(path), std::runtime_error);
}

#include <gtest/gtest.h>

#include "router.hpp"

namespace buddy::speakers {
namespace {

using K = Action::Kind;
using namespace std::chrono_literals;

const std::string kMain = "00:11:22:33:44:01";
const std::string kBath = "00:11:22:33:44:02";
const std::string kMainSink = "bluez_output.00_11_22_33_44_01.1";
const std::string kBathSink = "bluez_output.00_11_22_33_44_02.1";

std::vector<Speaker> nests() {
    return {{speaker_id(kBath), "Bathroom speaker", kBath}, {speaker_id(kMain), "Main speaker", kMain}};
}

World world(bool music, std::vector<std::string> connected = {}) {
    World w;
    w.music = music;
    for (const auto& mac : connected) w.connected.push_back({mac, mac == kMain ? kMainSink : kBathSink});
    return w;
}

bool has(const std::vector<Action>& actions, K kind, const std::string& target = "") {
    for (const auto& a : actions)
        if (a.kind == kind && (target.empty() || a.target == target)) return true;
    return false;
}

struct RouterTest : ::testing::Test {
    RouterTest() : r(nests(), {}, "main speaker") { r.step(world(false), t); }  // settle: restore + voice default
    // Music starts at t, Main connects at t + 3 s, its volume is set at t + 6 s
    void play_on_main() {
        r.step(world(true), t);
        r.step(world(true, {kMain}), t + 3s);
        r.step(world(true, {kMain}), t + 6s);
    }
    Router r;
    Clock::time_point t = Clock::time_point{} + 1h;
};

TEST_F(RouterTest, DefaultSpeakerByNameElseTheFirst) {
    EXPECT_EQ(r.selected()->name, "Main speaker");
    EXPECT_EQ(Router(nests(), {}, "").selected()->name, "Bathroom speaker");
    EXPECT_EQ(Router(nests(), {}, "kitchen").selected()->name, "Bathroom speaker");
    EXPECT_EQ(Router({}, {}, "main speaker").selected(), nullptr);
}

TEST_F(RouterTest, FirstStepRestoresTheMusicVolumeAndSendsVoiceToTheDefault) {
    Router fresh(nests(), {}, "");
    const auto a = fresh.step(world(false), t);
    EXPECT_TRUE(has(a, K::SetVolume, "buddy_music"));
    EXPECT_TRUE(has(a, K::MoveVoice));
    EXPECT_TRUE(fresh.step(world(false), t + 1s).empty());
}

TEST_F(RouterTest, NothingWithoutMusic) { EXPECT_TRUE(r.step(world(false), t + 1s).empty()); }

TEST_F(RouterTest, MusicConnectsTheSelectedNestThenLinksItAndMovesTheVoice) {
    const auto a = r.step(world(true), t);
    ASSERT_EQ(a.size(), 1u);
    EXPECT_EQ(a[0], (Action{K::Connect, kMain}));
    const auto b = r.step(world(true, {kMain}), t + 3s);
    EXPECT_EQ(b, (std::vector<Action>{{K::LinkMusic, kMainSink}, {K::MoveVoice, kMainSink}, {K::SetVolume, "buddy_voice", 60}}));
    // The Nest syncs its own volume right after connecting: ours comes later
    EXPECT_EQ(r.deadline(), t + 6s);
    EXPECT_TRUE(r.step(world(true, {kMain}), t + 4s).empty());
    EXPECT_EQ(r.step(world(true, {kMain}), t + 6s), (std::vector<Action>{{K::SetVolume, kMainSink, 80}}));
    EXPECT_TRUE(r.step(world(true, {kMain}), t + 9s).empty());
    EXPECT_EQ(r.state()["voice"], speaker_id(kMain));
    EXPECT_EQ(r.state()["music"], "playing");
}

TEST_F(RouterTest, PauseSendsTheVoiceBackAndDisconnectsAfterIdle) {
    play_on_main();
    EXPECT_TRUE(r.step(world(false, {kMain}), t + 10s).empty());  // maybe just "next"
    EXPECT_EQ(r.deadline(), t + 12s);
    EXPECT_EQ(r.step(world(false, {kMain}), t + 12s),
              (std::vector<Action>{{K::MoveVoice, ""}, {K::SetVolume, "buddy_voice", 100}}));
    EXPECT_EQ(r.state()["music"], "idle");
    EXPECT_EQ(r.deadline(), t + 12s + 600s);
    EXPECT_TRUE(r.step(world(false, {kMain}), t + 9min).empty());
    EXPECT_EQ(r.step(world(false, {kMain}), t + 12s + 600s), (std::vector<Action>{{K::Disconnect, kMain}}));
    EXPECT_FALSE(r.step(world(false), t + 11min).size());
    EXPECT_EQ(r.deadline(), std::nullopt);
}

TEST_F(RouterTest, AShortGapIsNoPause) {  // Soloist pauses briefly on "next" and on transfers
    play_on_main();
    EXPECT_TRUE(r.step(world(false, {kMain}), t + 10s).empty());
    EXPECT_TRUE(r.step(world(true, {kMain}), t + 11s).empty());
    EXPECT_EQ(r.deadline(), std::nullopt);
    EXPECT_EQ(r.state()["voice"], speaker_id(kMain));
}

TEST_F(RouterTest, ResumeWithinIdleKeepsTheConnection) {
    play_on_main();
    r.step(world(false, {kMain}), t + 10s);
    r.step(world(false, {kMain}), t + 12s);
    EXPECT_EQ(r.step(world(true, {kMain}), t + 5min),
              (std::vector<Action>{{K::MoveVoice, kMainSink}, {K::SetVolume, "buddy_voice", 60}}));
}

TEST_F(RouterTest, SwitchingNestsDisconnectsTheOtherFirst) {
    play_on_main();
    EXPECT_EQ(r.select("bathroom"), "");
    const auto a = r.step(world(true, {kMain}), t + 7s);
    EXPECT_EQ(a, (std::vector<Action>{{K::Disconnect, kMain}, {K::Connect, kBath}, {K::MoveVoice, ""},
                                      {K::SetVolume, "buddy_voice", 100}}));
    // Main gone: its links went with it, WirePlumber sent the voice to the default
    const auto b = r.step(world(true, {kBath}), t + 8s);
    EXPECT_EQ(b, (std::vector<Action>{{K::LinkMusic, kBathSink}, {K::MoveVoice, kBathSink}, {K::SetVolume, "buddy_voice", 60}}));
    EXPECT_EQ(r.step(world(true, {kBath}), t + 11s), (std::vector<Action>{{K::SetVolume, kBathSink, 80}}));
}

TEST_F(RouterTest, AFailedConnectIsNotRetriedUntilTheMusicRestarts) {
    r.step(world(true), t);
    r.connect_failed(kMain);
    EXPECT_TRUE(r.step(world(true), t + 20s).empty());
    r.step(world(false), t + 30s);
    r.step(world(false), t + 32s);
    EXPECT_EQ(r.step(world(true), t + 40s), (std::vector<Action>{{K::Connect, kMain}}));
}

TEST_F(RouterTest, ANestThatDropsOutIsReconnected) {
    play_on_main();
    const auto a = r.step(world(true), t + 60s);  // out of range, Nest rebooted…
    EXPECT_TRUE(has(a, K::Connect, kMain));
    EXPECT_FALSE(has(a, K::MoveVoice));  // WirePlumber already moved it back to the default
    EXPECT_TRUE(has(a, K::SetVolume, "buddy_voice"));  // at full volume again
    EXPECT_EQ(r.state()["voice"], "jabra");
    EXPECT_TRUE(has(r.step(world(true, {kMain}), t + 63s), K::LinkMusic, kMainSink));
    EXPECT_TRUE(has(r.step(world(true, {kMain}), t + 66s), K::SetVolume, kMainSink));  // again: new connection
}

TEST_F(RouterTest, ManualConnectWithoutMusicThenDisconnectStaysOff) {
    EXPECT_EQ(r.connect("bathroom speaker", t), "");
    EXPECT_EQ(r.step(world(false), t), (std::vector<Action>{{K::Connect, kBath}}));
    const auto a = r.step(world(false, {kBath}), t + 3s);
    EXPECT_TRUE(has(a, K::LinkMusic, kBathSink));
    EXPECT_FALSE(has(a, K::MoveVoice));  // no music: voice stays on the Jabra
    r.disconnect();
    EXPECT_EQ(r.step(world(false, {kBath}), t + 5s), (std::vector<Action>{{K::Disconnect, kBath}}));
    EXPECT_TRUE(r.step(world(false), t + 6s).empty());
}

TEST_F(RouterTest, DisconnectWhilePlayingHoldsUntilTheMusicRestarts) {
    r.step(world(true), t);
    r.step(world(true, {kMain}), t + 3s);
    r.disconnect();
    EXPECT_TRUE(has(r.step(world(true, {kMain}), t + 5s), K::Disconnect, kMain));
    EXPECT_TRUE(r.step(world(true), t + 6s).size() <= 1u);  // at most the voice back
    EXPECT_FALSE(has(r.step(world(true), t + 7s), K::Connect));
    r.step(world(false), t + 8s);
    r.step(world(false), t + 10s);
    EXPECT_TRUE(has(r.step(world(true), t + 12s), K::Connect, kMain));
}

TEST_F(RouterTest, UnknownAndAmbiguousNames) {
    EXPECT_EQ(r.select("kitchen"), "I don't know a speaker called kitchen.");
    EXPECT_EQ(r.select("speaker"), "Which speaker, Bathroom speaker or Main speaker?");
    EXPECT_EQ(r.select(speaker_id(kBath)), "");
    EXPECT_EQ(r.selected()->name, "Bathroom speaker");
}

TEST_F(RouterTest, DucksWhileBuddyListensOrSpeaksAndRestoresAfterTheHold) {
    r.set_listening(true);
    EXPECT_EQ(r.step(world(false), t), (std::vector<Action>{{K::SetVolume, "buddy_music", 30}}));
    r.set_listening(false);
    r.set_speaking(true);
    EXPECT_TRUE(r.step(world(false), t + 2s).empty());  // still busy
    r.set_speaking(false);
    EXPECT_TRUE(r.step(world(false), t + 3s).empty());  // hold starts
    EXPECT_EQ(r.deadline(), t + 3s + 1500ms);
    EXPECT_TRUE(r.step(world(false), t + 4s).empty());
    EXPECT_EQ(r.step(world(false), t + 4500ms), (std::vector<Action>{{K::SetVolume, "buddy_music", 100}}));
    EXPECT_EQ(r.state()["ducked"], false);
}

TEST_F(RouterTest, VoiceIsNotMovedMidSentence) {
    r.step(world(true), t);
    r.set_speaking(true);  // "Moving the music to Main speaker." while it connects
    const auto a = r.step(world(true, {kMain}), t + 3s);
    EXPECT_FALSE(has(a, K::MoveVoice));
    r.set_speaking(false);
    EXPECT_TRUE(has(r.step(world(true, {kMain}), t + 4s), K::MoveVoice, kMainSink));
}

TEST_F(RouterTest, StateListsTheNests) {
    r.step(world(true), t);
    r.step(world(true, {kMain}), t + 3s);
    const auto s = r.state();
    ASSERT_EQ(s["speakers"].size(), 2u);
    EXPECT_EQ(s["speakers"][0]["name"], "Bathroom speaker");
    EXPECT_EQ(s["speakers"][0]["connected"], false);
    EXPECT_EQ(s["speakers"][1]["connected"], true);
    EXPECT_EQ(s["selected"], speaker_id(kMain));
}

} // namespace
} // namespace buddy::speakers

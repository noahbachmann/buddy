#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "audio_source.hpp"
#include "endpoint.hpp"

using namespace buddy::ears;

namespace {

// Plays back a fixed list of probabilities, one per frame; the last one repeats.
struct ScriptedModel : SpeechModel {
    std::vector<float> probs;
    std::size_t next = 0;
    int resets = 0;
    float process(std::span<const float>) override { return probs[std::min(next++, probs.size() - 1)]; }
    void reset() override {
        next = 0;
        ++resets;
    }
};

// `frames` × p, for building scripts: script({{20, 0.9f}, {1, 0.0f}})
std::vector<float> script(std::initializer_list<std::pair<int, float>> parts) {
    std::vector<float> out;
    for (const auto& [n, p] : parts) out.insert(out.end(), n, p);
    return out;
}

// One 80 ms chunk at a constant level (DC: its RMS is exactly the amplitude).
Chunk chunk_at(float db) {
    return Chunk(kChunkSamples, static_cast<int16_t>(std::lround(32768.0 * std::pow(10.0, db / 20.0))));
}

// Feeds chunks until the endpointer decides; returns the result and how long it took.
std::pair<Endpointer::Result, int> run(Endpointer& e, float db = -20.0f, int max_ms = 10000) {
    const Chunk c = chunk_at(db);
    for (int ms = 80; ms <= max_ms; ms += 80) {
        const auto r = e.process(c);
        if (r != Endpointer::Result::Continue) return {r, ms};
    }
    return {Endpointer::Result::Continue, max_ms};
}

const EndpointConfig kConfig{.threshold = 0.5f, .silence_ms = 700, .start_timeout_ms = 4000, .max_ms = 5000};

} // namespace

TEST(Endpointer, DoneAfterSilence) {
    ScriptedModel vad;
    vad.probs = script({{20, 0.9f}, {1, 0.0f}});  // 640 ms speech
    Endpointer e(vad, kConfig);
    e.start();
    const auto [result, ms] = run(e);
    EXPECT_EQ(result, Endpointer::Result::Done);
    EXPECT_NEAR(ms, 640 + 700, 80);
    EXPECT_EQ(e.speech_ms(), 640);
}

TEST(Endpointer, NoSpeechAfterTheStartTimeout) {
    ScriptedModel vad;
    vad.probs = {0.0f};
    Endpointer e(vad, kConfig);
    e.start();
    const auto [result, ms] = run(e);
    EXPECT_EQ(result, Endpointer::Result::NoSpeech);
    EXPECT_NEAR(ms, 4000, 80);
}

TEST(Endpointer, ClicksDontStartACommand) {
    ScriptedModel vad;
    vad.probs = script({{2, 0.9f}, {1, 0.0f}});  // 64 ms < the 96 ms minimum
    Endpointer e(vad, kConfig);
    e.start();
    EXPECT_EQ(run(e).first, Endpointer::Result::NoSpeech);
}

TEST(Endpointer, TooLong) {
    ScriptedModel vad;
    vad.probs = {0.9f};
    Endpointer e(vad, kConfig);
    e.start();
    const auto [result, ms] = run(e);
    EXPECT_EQ(result, Endpointer::Result::TooLong);
    EXPECT_NEAR(ms, 5000, 80);
}

TEST(Endpointer, HysteresisKeepsSpeechBetweenTheThresholds) {
    ScriptedModel vad;
    vad.probs = script({{10, 0.9f}, {30, 0.4f}, {1, 0.2f}});  // 0.4: below 0.5 but above 0.5 − 0.15
    Endpointer e(vad, kConfig);
    e.start();
    EXPECT_EQ(run(e).first, Endpointer::Result::Done);
    EXPECT_EQ(e.speech_ms(), 40 * 32);
}

TEST(Endpointer, SpeechBelowTheLevelGateIsSilence) {
    ScriptedModel vad;
    vad.probs = {0.9f};  // the TV talks all the time
    Endpointer e(vad, kConfig);
    LevelGate gate;
    gate.gate_db = -30.0f;

    e.start(gate);
    EXPECT_EQ(run(e, -40.0f).first, Endpointer::Result::NoSpeech);  // TV only
    e.start(gate);
    EXPECT_EQ(run(e, -20.0f).first, Endpointer::Result::TooLong);  // the user, louder than the gate
}

TEST(Endpointer, StartResetsEverything) {
    ScriptedModel vad;
    vad.probs = script({{20, 0.9f}, {1, 0.0f}});
    Endpointer e(vad, kConfig);
    e.start();
    run(e);
    e.start();
    EXPECT_EQ(vad.resets, 2);
    EXPECT_EQ(e.speech_ms(), 0);
    EXPECT_EQ(run(e).first, Endpointer::Result::Done);
}

// --- level gate ---

namespace {

// 25 chunks (2 s) background, then 15 chunks (1.2 s) wake word.
LevelHistory history(float background_db, float voice_db) {
    LevelHistory h;
    for (int i = 0; i < 25; ++i) h.push(chunk_at(background_db));
    for (int i = 0; i < 15; ++i) h.push(chunk_at(voice_db));
    return h;
}

} // namespace

TEST(LevelHistory, GateBetweenBackgroundAndVoice) {
    const LevelGate g = history(-50.0f, -20.0f).gate(0.4f);
    EXPECT_NEAR(g.background_db, -50.0f, 0.1f);
    EXPECT_NEAR(g.voice_db, -20.0f, 0.1f);
    EXPECT_NEAR(g.gate_db, -38.0f, 0.1f);
    EXPECT_TRUE(g.active());
}

TEST(LevelHistory, OffWithoutEnoughHistory) {
    LevelHistory h;
    for (int i = 0; i < 39; ++i) h.push(chunk_at(-20.0f));
    const LevelGate g = h.gate(0.4f);
    EXPECT_FALSE(g.measured());
    EXPECT_FALSE(g.active());
}

TEST(LevelHistory, OffWhenTheUserIsntClearlyLouder) {
    const LevelGate g = history(-30.0f, -25.0f).gate(0.4f);  // 5 dB < 6 dB contrast
    EXPECT_TRUE(g.measured());
    EXPECT_FALSE(g.active());
    EXPECT_FALSE(history(-50.0f, -20.0f).gate(0.0f).active());  // position 0 = off
}

TEST(LevelHistory, KeepsOnlyTheLast3Point2Seconds) {
    LevelHistory h;
    for (int i = 0; i < 100; ++i) h.push(chunk_at(-10.0f));  // old and loud: must not count
    for (int i = 0; i < 25; ++i) h.push(chunk_at(-50.0f));
    for (int i = 0; i < 15; ++i) h.push(chunk_at(-20.0f));
    EXPECT_NEAR(h.gate(0.4f).background_db, -50.0f, 0.1f);
}

TEST(LevelDb, Range) {
    EXPECT_NEAR(level_db(Chunk(kChunkSamples, 32767)), 0.0f, 0.01f);
    EXPECT_NEAR(level_db(Chunk(kChunkSamples, 0)), -90.0f, 0.01f);
}

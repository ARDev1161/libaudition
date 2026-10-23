#include "../demo/qt/adaptive_array_presence_gate.hpp"
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
std::vector<float> interleaved(std::size_t frames, float mic_amplitude,
                               float aux_amplitude = 0.0F, bool single_mic = false) {
    std::vector<float> pcm(frames * 6U);
    for (std::size_t i = 0; i < frames; ++i) {
        const float wave = static_cast<float>(
            std::sin(0.2 * static_cast<double>(i)));
        for (std::size_t channel = 0; channel < 6U; ++channel) {
            pcm[i * 6U + channel] = (channel == 0U || channel == 5U)
                ? aux_amplitude * wave
                : (single_mic && channel != 1U ? 0.001F : mic_amplitude) * wave;
        }
    }
    return pcm;
}
}

TEST(AdaptiveArrayPresenceGate, LearnsRoomFloorAndReleasesGhostTracks) {
    demo::AdaptiveArrayPresenceGate gate{16000U};
    const auto room = interleaved(128U, 0.001F);
    demo::AdaptiveArrayPresenceGate::Status state;
    for (int i = 0; i < 420; ++i)
        state = gate.update(room.data(), 128U, 6U, {1U, 2U, 3U, 4U});
    EXPECT_FALSE(state.process_audio);
    EXPECT_FALSE(state.calibrating);
    EXPECT_NEAR(state.above_noise_db, 0.0, 0.1);
}

TEST(AdaptiveArrayPresenceGate, QuietAboveNoiseAcquiredWithoutLoudOnset) {
    demo::AdaptiveArrayPresenceGate gate{16000U};
    const auto room = interleaved(128U, 0.001F);
    for (int i = 0; i < 420; ++i)
        static_cast<void>(gate.update(room.data(), 128U, 6U, {1U,2U,3U,4U}));
    const auto speech = interleaved(128U, 0.004F);
    const auto state = gate.update(speech.data(), 128U, 6U, {1U,2U,3U,4U});
    EXPECT_TRUE(state.process_audio);
    EXPECT_GT(state.above_noise_db, 7.0);
}

TEST(AdaptiveArrayPresenceGate, ReleasesThenReopensWithStableIdentity) {
    demo::AdaptiveArrayPresenceGate gate{16000U};
    const auto room = interleaved(128U, 0.001F);
    for (int i = 0; i < 420; ++i)
        static_cast<void>(gate.update(room.data(), 128U, 6U, {1U,2U,3U,4U}));
    const auto speech = interleaved(128U, 0.004F);
    auto state = gate.update(speech.data(), 128U, 6U, {1U,2U,3U,4U});
    EXPECT_TRUE(state.process_audio);
    int release_count = 0;
    for (int i = 0; i < 240; ++i) {
        state = gate.update(room.data(), 128U, 6U, {1U,2U,3U,4U});
        if (state.became_inactive) ++release_count;
    }
    EXPECT_EQ(release_count, 1);
    EXPECT_FALSE(state.process_audio);
    state = gate.update(speech.data(), 128U, 6U, {1U,2U,3U,4U});
    EXPECT_TRUE(state.process_audio);
    EXPECT_FALSE(state.became_inactive);
}

TEST(AdaptiveArrayPresenceGate, AuxAndSingleBadMicCannotLatchActivity) {
    demo::AdaptiveArrayPresenceGate gate{16000U};
    const auto room = interleaved(128U, 0.001F);
    for (int i = 0; i < 420; ++i)
        static_cast<void>(gate.update(room.data(), 128U, 6U, {1U,2U,3U,4U}));
    const auto noise = interleaved(128U, 0.015F, 0.8F, true);
    const auto state = gate.update(noise.data(), 128U, 6U, {1U,2U,3U,4U});
    EXPECT_FALSE(state.process_audio);
}

TEST(AdaptiveArrayPresenceGate, XrunResetAndBadInput) {
    demo::AdaptiveArrayPresenceGate gate{16000U};
    const auto room = interleaved(128U, 0.001F);
    for (int i = 0; i < 420; ++i)
        static_cast<void>(gate.update(room.data(), 128U, 6U, {1U,2U,3U,4U}));
    gate.reset();
    const auto state = gate.update(room.data(), 128U, 6U, {1U,2U,3U,4U});
    EXPECT_TRUE(state.calibrating);
    EXPECT_THROW(static_cast<void>(gate.update(nullptr, 128U, 6U, {1U})),
                 std::invalid_argument);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(static_cast<void>(gate.update(&nan, 1U, 1U, {0U})),
                 std::invalid_argument);
}

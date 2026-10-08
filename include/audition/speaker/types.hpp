#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <audition/core/id.hpp>
#include <audition/core/probability.hpp>
#include <audition/core/time.hpp>

namespace audition {

struct SpeakerEmbedding {
    std::string model_id{};
    std::vector<float> values{};
    std::optional<Probability> quality{};
};

struct SpeakerVerificationResult {
    Score similarity{};
    bool matched{false};
    std::optional<Probability> calibrated_probability{};
};

struct SpeakerIdentity {
    SpeakerId speaker_id{};
    std::string display_name{};
    Score similarity{};
    std::optional<Probability> calibrated_probability{};
};

struct SpeakerEnrollment {
    SpeakerId speaker_id{};
    std::string display_name{};
    std::vector<SpeakerEmbedding> embeddings{};
};

struct SpeakerDiarizationSegment {
    std::uint32_t speaker_index{0};
    Duration start_offset{};
    Duration end_offset{};
    std::optional<Score> confidence{};
};

struct SpeakerDiarizationResult {
    std::vector<SpeakerDiarizationSegment> segments{};
    std::uint32_t speaker_count{0};
};

}  // namespace audition

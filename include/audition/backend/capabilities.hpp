#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <audition/audio/audio_format.hpp>
#include <audition/core/execution.hpp>

namespace audition {

struct BackendInfo {
    std::string name{};
    std::string implementation_version{};
};

struct AudioRequirements {
    std::vector<std::uint32_t> supported_sample_rates_hz{};  // empty = backend-defined/any
    std::uint32_t min_channels{1};
    std::uint32_t max_channels{1};
    std::vector<AudioLayout> supported_layouts{AudioLayout::Interleaved, AudioLayout::Planar};
    std::optional<std::size_t> preferred_frame_count{};
};

struct ExecutionCapabilities {
    std::vector<DeviceClass> device_classes{DeviceClass::Cpu};
    std::vector<std::string> providers{};
};

struct AsrCapabilities {
    bool token_timestamps{false};
    bool word_timestamps{false};
    bool language_identification{false};
    bool partial_results{false};
    bool endpoint_detection{false};
    AudioRequirements audio{};
    ExecutionCapabilities execution{};
};

struct SpatialCapabilities {
    bool localization{true};
    bool tracking{true};
    bool separation{true};
    bool supports_3d{true};
    AudioRequirements audio{};
};

struct ClassifierCapabilities {
    bool open_vocabulary{false};
    bool embeddings{false};
    AudioRequirements audio{};
    ExecutionCapabilities execution{};
};

struct TtsCapabilities {
    bool streaming{false};
    bool voice_cloning{false};
    bool style_control{false};
    std::vector<std::string> languages{};
    ExecutionCapabilities execution{};
};

}  // namespace audition

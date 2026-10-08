#include <audition/backends/sherpa/language_identifier.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>

#include "detail/sherpa_config.hpp"

namespace audition {

class SherpaLanguageIdentifier::Impl {
public:
    explicit Impl(SherpaLanguageIdOptions options)
        : options_(std::move(options)),
          config_(sherpa_detail::makeLanguageIdConfig(options_)),
          identifier_(sherpa_onnx::cxx::SpokenLanguageIdentification::Create(config_)) {
        if (identifier_.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx language identifier"};
        }
    }

    std::vector<LanguageScore> identify(AudioView speech) const {
        sherpa_detail::validateMonoAudio(
            speech, options_.sample_rate_hz, "language identification");
        if (speech.sampleCount() == 0U) {
            return {};
        }
        if (speech.sampleCount() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Sherpa language-ID input exceeds backend range"};
        }

        std::lock_guard<std::mutex> lock{mutex_};
        auto stream = identifier_.CreateStream();
        if (stream.Get() == nullptr) {
            throw Error{ErrorCode::ProcessingError,
                        "Failed to create sherpa-onnx language-ID stream"};
        }
        stream.AcceptWaveform(
            static_cast<std::int32_t>(speech.format().sample_rate_hz),
            speech.data(),
            static_cast<std::int32_t>(speech.sampleCount()));

        const auto result = identifier_.Compute(&stream);
        if (result.lang.empty()) {
            return {};
        }

        LanguageScore score{};
        score.language = result.lang;
        score.probability.reset();
        return {std::move(score)};
    }

    SherpaLanguageIdOptions options_{};
    sherpa_onnx::cxx::SpokenLanguageIdentificationConfig config_{};
    sherpa_onnx::cxx::SpokenLanguageIdentification identifier_;
    mutable std::mutex mutex_{};
};

SherpaLanguageIdentifier::SherpaLanguageIdentifier(SherpaLanguageIdOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaLanguageIdentifier::~SherpaLanguageIdentifier() = default;
SherpaLanguageIdentifier::SherpaLanguageIdentifier(SherpaLanguageIdentifier&&) noexcept = default;
SherpaLanguageIdentifier& SherpaLanguageIdentifier::operator=(
    SherpaLanguageIdentifier&&) noexcept = default;

BackendInfo SherpaLanguageIdentifier::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

AudioRequirements SherpaLanguageIdentifier::audioRequirements() const {
    return sherpa_detail::monoRequirements(impl_->options_.sample_rate_hz);
}

std::vector<LanguageScore> SherpaLanguageIdentifier::identify(AudioView speech) const {
    return impl_->identify(speech);
}

const SherpaLanguageIdOptions& SherpaLanguageIdentifier::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition

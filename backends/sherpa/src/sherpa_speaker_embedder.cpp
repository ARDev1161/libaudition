#include <audition/backends/sherpa/speaker_embedder.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/sherpa_config.hpp"
#include "detail/sherpa_speaker_config.hpp"

namespace audition {

class SherpaSpeakerEmbedder::Impl {
public:
    explicit Impl(SherpaSpeakerEmbeddingOptions options)
        : options_(std::move(options)),
          extractor_(sherpa_onnx::cxx::SpeakerEmbeddingExtractor::Create(
              sherpa_detail::makeSpeakerEmbeddingConfig(options_))),
          model_id_(sherpa_detail::speakerModelId(options_)) {
        if (!extractor_.Get()) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create Sherpa speaker embedding extractor"};
        }
        const auto dim = extractor_.Dim();
        if (dim <= 0) {
            throw Error{ErrorCode::ModelLoadError,
                        "Sherpa speaker embedding extractor returned invalid dimension"};
        }
        dimension_ = static_cast<std::size_t>(dim);
    }

    [[nodiscard]] AudioRequirements requirements() const {
        return sherpa_detail::monoRequirements(options_.sample_rate_hz);
    }

    [[nodiscard]] std::optional<SpeakerEmbedding> embed(AudioView speech) const {
        sherpa_detail::validateMonoAudio(
            speech, options_.sample_rate_hz, "speaker embedding");

        std::lock_guard<std::mutex> lock{mutex_};
        auto stream = extractor_.CreateStream();
        stream.AcceptWaveform(
            static_cast<std::int32_t>(options_.sample_rate_hz),
            speech.data(),
            speech.sampleCount());
        stream.InputFinished();

        if (!extractor_.IsReady(&stream)) {
            return std::nullopt;
        }

        auto values = extractor_.ComputeEmbedding(&stream);
        if (values.size() != dimension_) {
            throw Error{ErrorCode::ProcessingError,
                        "Sherpa speaker embedding dimension changed unexpectedly"};
        }

        SpeakerEmbedding result{};
        result.model_id = model_id_;
        result.values = std::move(values);
        result.quality.reset();
        return result;
    }

    SherpaSpeakerEmbeddingOptions options_{};
    sherpa_onnx::cxx::SpeakerEmbeddingExtractor extractor_;
    std::string model_id_{};
    std::size_t dimension_{0};
    mutable std::mutex mutex_{};
};

SherpaSpeakerEmbedder::SherpaSpeakerEmbedder(SherpaSpeakerEmbeddingOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaSpeakerEmbedder::~SherpaSpeakerEmbedder() = default;
SherpaSpeakerEmbedder::SherpaSpeakerEmbedder(SherpaSpeakerEmbedder&&) noexcept = default;
SherpaSpeakerEmbedder& SherpaSpeakerEmbedder::operator=(SherpaSpeakerEmbedder&&) noexcept = default;

BackendInfo SherpaSpeakerEmbedder::backendInfo() const {
    return {"sherpa-onnx-speaker-embedding", "99ddefaa"};
}
AudioRequirements SherpaSpeakerEmbedder::audioRequirements() const {
    return impl_->requirements();
}
std::size_t SherpaSpeakerEmbedder::embeddingDimension() const {
    return impl_->dimension_;
}
std::optional<SpeakerEmbedding> SherpaSpeakerEmbedder::embed(AudioView speech) const {
    return impl_->embed(speech);
}
const SherpaSpeakerEmbeddingOptions& SherpaSpeakerEmbedder::options() const noexcept {
    return impl_->options_;
}

}  // namespace audition

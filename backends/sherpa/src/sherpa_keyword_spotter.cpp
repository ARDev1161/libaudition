#include <audition/backends/sherpa/keyword_spotter.hpp>

#include <cmath>
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
namespace {

Duration secondsToDuration(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0F) {
        throw Error{ErrorCode::ProcessingError,
                    "Sherpa KWS returned an invalid start time"};
    }
    return Duration{static_cast<std::int64_t>(
        std::llround(static_cast<double>(seconds) * 1.0e9))};
}

struct KeywordSharedState {
    explicit KeywordSharedState(const SherpaKeywordSpotterOptions& requested_options)
        : options(requested_options),
          config(sherpa_detail::makeKeywordSpotterConfig(requested_options)),
          spotter(sherpa_onnx::cxx::KeywordSpotter::Create(config)) {
        if (spotter.Get() == nullptr) {
            throw Error{ErrorCode::ModelLoadError,
                        "Failed to create sherpa-onnx keyword spotter"};
        }
    }

    SherpaKeywordSpotterOptions options{};
    sherpa_onnx::cxx::KeywordSpotterConfig config{};
    sherpa_onnx::cxx::KeywordSpotter spotter;
    mutable std::mutex mutex{};
};

class SherpaKeywordSession final : public IKeywordSpotterSession {
public:
    explicit SherpaKeywordSession(std::shared_ptr<KeywordSharedState> state)
        : state_(std::move(state)) {
        reset();
    }

    void reset() override {
        std::lock_guard<std::mutex> lock{state_->mutex};
        stream_ = state_->spotter.CreateStream();
        if (stream_.Get() == nullptr) {
            throw Error{ErrorCode::ProcessingError,
                        "Failed to create sherpa-onnx keyword stream"};
        }
    }

    std::vector<KeywordHit> process(AudioView audio) override {
        sherpa_detail::validateMonoAudio(
            audio, state_->options.features.sample_rate_hz, "keyword spotting");
        if (audio.sampleCount() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
            throw Error{ErrorCode::InvalidArgument,
                        "Sherpa KWS chunk exceeds backend range"};
        }

        std::vector<KeywordHit> hits;
        std::lock_guard<std::mutex> lock{state_->mutex};
        if (audio.sampleCount() > 0U) {
            stream_.AcceptWaveform(
                static_cast<std::int32_t>(audio.format().sample_rate_hz),
                audio.data(),
                static_cast<std::int32_t>(audio.sampleCount()));
        }

        while (state_->spotter.IsReady(&stream_)) {
            state_->spotter.Decode(&stream_);
            const auto native = state_->spotter.GetResult(&stream_);
            if (!native.keyword.empty()) {
                KeywordHit hit{};
                hit.keyword = native.keyword;
                hit.offset = secondsToDuration(native.start_time);
                hit.probability.reset();
                hits.push_back(std::move(hit));
                state_->spotter.Reset(&stream_);
            }
        }
        return hits;
    }

private:
    std::shared_ptr<KeywordSharedState> state_;
    sherpa_onnx::cxx::OnlineStream stream_{nullptr};
};

}  // namespace

class SherpaKeywordSpotter::Impl {
public:
    explicit Impl(SherpaKeywordSpotterOptions options)
        : state_(std::make_shared<KeywordSharedState>(std::move(options))) {}

    std::shared_ptr<KeywordSharedState> state_;
};

SherpaKeywordSpotter::SherpaKeywordSpotter(SherpaKeywordSpotterOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
SherpaKeywordSpotter::~SherpaKeywordSpotter() = default;
SherpaKeywordSpotter::SherpaKeywordSpotter(SherpaKeywordSpotter&&) noexcept = default;
SherpaKeywordSpotter& SherpaKeywordSpotter::operator=(SherpaKeywordSpotter&&) noexcept = default;

BackendInfo SherpaKeywordSpotter::backendInfo() const {
    return {"sherpa-onnx", sherpa_onnx::cxx::GetVersionStr()};
}

AudioRequirements SherpaKeywordSpotter::audioRequirements() const {
    return sherpa_detail::monoRequirements(impl_->state_->options.features.sample_rate_hz);
}

std::unique_ptr<IKeywordSpotterSession> SherpaKeywordSpotter::createSession() const {
    return std::make_unique<SherpaKeywordSession>(impl_->state_);
}

const SherpaKeywordSpotterOptions& SherpaKeywordSpotter::options() const noexcept {
    return impl_->state_->options;
}

}  // namespace audition

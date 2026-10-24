#include <audition/backends/whispercpp/asr.hpp>
#include <audition/core/error.hpp>
#include <whisper.h>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace audition {

class WhisperCppAsr::Impl {
public:
    explicit Impl(WhisperCppAsrOptions options) : options_(std::move(options)) {
        if (options_.model_path.empty() || options_.threads < 1)
            throw std::invalid_argument{"whisper.cpp requires a model path and positive thread count"};
        auto parameters = whisper_context_default_params();
        parameters.use_gpu = options_.use_gpu;
        context_ = whisper_init_from_file_with_params(options_.model_path.c_str(), parameters);
        if (!context_)
            throw Error{ErrorCode::ModelLoadError, "whisper.cpp failed to load model"};
    }

    ~Impl() { if (context_) whisper_free(context_); }

    Transcript transcribe(const SpeechSegment& segment) const {
        const auto input = segment.audio.view();
        if (input.format().sample_rate_hz != WHISPER_SAMPLE_RATE ||
            input.format().channel_count != 1 ||
            input.format().layout != AudioLayout::Interleaved ||
            input.sampleCount() == 0 ||
            input.sampleCount() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw Error{ErrorCode::InvalidArgument,
                        "whisper.cpp requires nonempty mono interleaved 16 kHz PCM"};
        std::lock_guard<std::mutex> guard{lock_};
        auto parameters = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        parameters.n_threads = options_.threads;
        parameters.translate = false;
        parameters.no_context = true;
        parameters.print_progress = false;
        parameters.print_realtime = false;
        parameters.print_timestamps = false;
        parameters.language = options_.language == "auto" ? nullptr : options_.language.c_str();
        parameters.detect_language = options_.language == "auto";
        if (whisper_full(context_, parameters, input.data(),
                         static_cast<int>(input.sampleCount())) != 0)
            throw Error{ErrorCode::ProcessingError, "whisper.cpp inference failed"};
        Transcript result{};
        result.segment_id = segment.segment_id;
        const int detected = whisper_full_lang_id(context_);
        if (detected >= 0) {
            const char* name = whisper_lang_str(detected);
            if (name) result.language = name;
        }
        for (int i = 0; i < whisper_full_n_segments(context_); ++i) {
            const char* text = whisper_full_get_segment_text(context_, i);
            if (text) result.text += text;
        }
        return result;
    }
private:
    WhisperCppAsrOptions options_;
    whisper_context* context_{nullptr};
    mutable std::mutex lock_;
};

WhisperCppAsr::WhisperCppAsr(WhisperCppAsrOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
WhisperCppAsr::~WhisperCppAsr() = default;

BackendInfo WhisperCppAsr::backendInfo() const {
    return {"whisper.cpp", whisper_version()};
}
AsrCapabilities WhisperCppAsr::capabilities() const {
    AsrCapabilities caps{};
    caps.audio.supported_sample_rates_hz = {16000};
    caps.audio.supported_layouts = {AudioLayout::Interleaved};
    caps.language_identification = true;
    caps.execution.providers = {impl_->options_.use_gpu ? "gpu" : "cpu"};
    return caps;
}
Transcript WhisperCppAsr::transcribe(const SpeechSegment& segment) const {
    return impl_->transcribe(segment);
}
} // namespace audition

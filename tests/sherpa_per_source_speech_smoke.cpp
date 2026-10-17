#include <audition/backends/sherpa/offline_asr.hpp>
#include <audition/backends/sherpa/options.hpp>
#include <audition/speech/per_source_speech_runtime.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

class DeterministicVadSession final : public audition::IVadSession {
public:
    void reset() override {}
    audition::VadResult process(audition::AudioView audio) override {
        bool voiced = false;
        for (std::size_t i = 0; i < audio.sampleCount(); ++i)
            voiced = voiced || std::abs(audio.data()[i]) > 0.03F;
        return {std::nullopt, voiced};
    }
};

class DeterministicVad final : public audition::IVoiceActivityDetector {
public:
    audition::BackendInfo backendInfo() const override {
        return {"deterministic-test-vad", "1"};
    }
    audition::AudioRequirements audioRequirements() const override {
        audition::AudioRequirements requirements{};
        requirements.supported_sample_rates_hz = {16000U};
        return requirements;
    }
    std::unique_ptr<audition::IVadSession> createSession() const override {
        return std::make_unique<DeterministicVadSession>();
    }
};

void run(const char* encoder, const char* decoder, const char* tokens) {
    auto options = std::make_unique<audition::SherpaOfflineAsrOptions>();
    audition::SherpaOfflineWhisperModel whisper{};
    whisper.encoder = encoder;
    whisper.decoder = decoder;
    whisper.language = "en";
    options->model = std::move(whisper);
    options->tokens = tokens;
    options->features.sample_rate_hz = 16000U;
    auto asr = std::make_shared<audition::SherpaOfflineAsr>(std::move(*options));
    DeterministicVad vad{};
    audition::PerSourceSpeechOptions runtime_options{};
    runtime_options.min_speech_samples = 1600U;
    runtime_options.pre_roll_samples = 0U;
    runtime_options.post_roll_samples = 0U;
    audition::PerSourceSpeechRuntime runtime{vad, asr, runtime_options};

    constexpr double pi = 3.14159265358979323846;
    std::vector<float> voice(16000U);
    for (std::size_t i = 0; i < voice.size(); ++i)
        voice[i] = static_cast<float>(0.1 *
            std::sin(2.0 * pi * 440.0 * static_cast<double>(i) / 16000.0));
    std::vector<float> silence(512U, 0.0F);
    const auto view = [](const std::vector<float>& samples) {
        return audition::AudioView{samples.data(), samples.size(),
            {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    };
    for (const std::uint64_t id : {41U, 42U}) {
        runtime.push(id, view(voice));
        runtime.push(id, view(silence));
    }
    std::vector<audition::SourceSpeechResult> received;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{120};
    while (std::chrono::steady_clock::now() < deadline && received.size() < 2U) {
        auto results = runtime.takeResults();
        for (auto& result : results) received.push_back(std::move(result));
        if (received.size() < 2U)
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (received.size() != 2U ||
        received[0].track_id != 41U || received[1].track_id != 42U ||
        !received[0].transcript.segment_id.valid() ||
        !received[1].transcript.segment_id.valid() ||
        received[0].transcript.segment_id == received[1].transcript.segment_id) {
        throw std::runtime_error{"Real ONNX ASR did not preserve per-source identity"};
    }
    if (runtime.statistics().completed_segments != 2U ||
        runtime.statistics().dropped_segments != 0U) {
        throw std::runtime_error{"Real ONNX ASR runtime lost segments"};
    }
    std::cout << "PASS: Sherpa Whisper ONNX transcribed two independent source segments"
              << " (text content intentionally not asserted)\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: sherpa_per_source_speech_smoke ENCODER DECODER TOKENS\n";
            return 2;
        }
        run(argv[1], argv[2], argv[3]);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: per-source Sherpa ASR: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

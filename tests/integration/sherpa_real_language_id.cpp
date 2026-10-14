// Genuine multilingual Whisper spoken-language identification, via libaudition.
// Upstream labeled WAVs assert English and German; no heuristic or fake model.
#include <audition/backends/sherpa.hpp>
#include <audition/io/wav_io.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

std::string classify(audition::SherpaLanguageIdentifier& detector,
                     const std::filesystem::path& wav,
                     const std::string& expected) {
    require(std::filesystem::is_regular_file(wav),
            "Missing labeled language-identification WAV");
    auto loaded = audition::io::loadWav(wav);
    const auto& audio = loaded.audio;
    require(audio.format().sample_rate_hz == 16000U &&
            audio.format().channel_count == 1U &&
            audio.format().layout == audition::AudioLayout::Interleaved,
            "Language-ID fixture is not mono 16 kHz interleaved");
    require(audio.frameCount() > 16000U,
            "Language-ID fixture unexpectedly shorter than 1 second");

    const auto result = detector.identify(audio.view());
    require(result.size() == 1U,
            "Real Whisper Language-ID must return exactly one language");
    require(result.front().language == expected,
            "Real Whisper Language-ID predicted the wrong language");
    require(!result.front().probability.has_value(),
            "Language-ID adapter must not fabricate posterior confidence");
    return result.front().language;
}

void run(const std::filesystem::path& weights,
         const std::filesystem::path& waves,
         const std::filesystem::path& reportPath) {
    const auto encoder = weights / "tiny-encoder.int8.onnx";
    const auto decoder = weights / "tiny-decoder.int8.onnx";
    require(std::filesystem::is_regular_file(encoder) &&
            std::filesystem::is_regular_file(decoder),
            "Missing multilingual Whisper Tiny INT8 weights");

    audition::SherpaLanguageIdOptions options{};
    options.encoder = encoder;
    options.decoder = decoder;
    options.runtime.num_threads = 1;
    options.sample_rate_hz = 16000U;
    audition::SherpaLanguageIdentifier detector{options};
    require(detector.backendInfo().name == "sherpa-onnx",
            "Language-ID advertised wrong backend");

    constexpr audition::AudioFormat mono{
        16000U, 1U, audition::AudioLayout::Interleaved};
    const audition::AudioBuffer empty{{}, mono, audition::Timestamp{}};
    require(detector.identify(empty.view()).empty(),
            "Language-ID returned a prediction for empty input");

    const auto english = classify(detector, waves / "en-english.wav", "en");
    const auto german = classify(detector, waves / "de-german.wav", "de");
    const auto again = classify(detector, waves / "en-english.wav", "en");
    require(english == again && english != german,
            "Language-ID did not preserve input independence");

    const std::vector<float> invalid(800U, 0.0F);
    const audition::AudioView wrongRate{
        invalid.data(), invalid.size(),
        {8000U, 1U, audition::AudioLayout::Interleaved},
        audition::Timestamp{}};
    bool rejected = false;
    try {
        static_cast<void>(detector.identify(wrongRate));
    } catch (const audition::Error& error) {
        rejected = error.code() == audition::ErrorCode::UnsupportedFormat;
    }
    require(rejected, "Language-ID accepted an invalid sampling rate");

    std::ofstream report{reportPath};
    require(report.good(), "Failed opening Language-ID report");
    report << "model=whisper-tiny-multilingual-int8\n"
           << "english=" << english << '\n'
           << "german=" << german << '\n'
           << "repeat_english=" << again << '\n'
           << "invalid_rate_rejected=" << (rejected ? 1 : 0) << '\n';
    require(report.good(), "Failed writing Language-ID report");
    std::cout << "PASS: real multilingual Whisper Language-ID en/de/repeat, "
                 "empty input and invalid-rate rejection\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: sherpa_real_language_id MODEL_DIR WAV_DIR REPORT\n";
        return 2;
    }
    try {
        run(argv[1], argv[2], argv[3]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: Sherpa real multilingual language identification: "
                  << e.what() << '\n';
        return 1;
    }
}

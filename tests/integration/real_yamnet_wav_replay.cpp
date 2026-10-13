// Real ONNX YAMNet inference over a pinned PCM16 WAV, using library WAV IO.
// This checks reproducible inference and signal shape, not arbitrary labels.
#include <audition/backends/yamnet/audio_tagger.hpp>
#include <audition/io/wav_io.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(const audition::ClassificationResult& result) {
    if (result.classes.size() != 5U)
        throw std::runtime_error("YAMNet did not return five AudioSet classes");
    double prior = 2.0;
    for (const auto& classified : result.classes) {
        const double probability = classified.probability.value();
        if (classified.label.empty() || !std::isfinite(probability) ||
            probability < 0.0 || probability > 1.0 || probability > prior)
            throw std::runtime_error("Invalid YAMNet class label or probability");
        prior = probability;
    }
}
std::vector<float> take(const std::vector<float>& input, std::size_t limit) {
    if (input.size() < limit) throw std::runtime_error("WAV is too short");
    return {input.begin(), input.begin() + static_cast<std::ptrdiff_t>(limit)};
}
int run(const char* modelPath, const char* labelsPath,
        const char* wavPath, const char* outPath) {
    const auto wav = audition::io::loadWav(wavPath);
    const auto format = wav.audio.format();
    if (format.channel_count != 1U || format.sample_rate_hz != 16000U ||
        format.layout != audition::AudioLayout::Interleaved ||
        wav.audio.frameCount() < 80000U)
        throw std::runtime_error("Expected at least five seconds of mono 16k PCM16 WAV");

    audition::YamnetOnnxOptions settings{};
    settings.model = modelPath;
    settings.labels = labelsPath;
    settings.top_k = 5U;
    const audition::YamnetAudioTagger model{settings};
    if (!model.supportsWindowSamples(15600U) ||
        !model.supportsWindowSamples(80000U))
        throw std::runtime_error("Pinned YAMNet does not support quick/refined windows");

    auto quickPcm = take(wav.audio.samples(), 15600U);
    auto longPcm = take(wav.audio.samples(), 80000U);
    const audition::AudioBuffer quick{std::move(quickPcm),
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const audition::AudioBuffer refined{std::move(longPcm),
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};

    const auto quickResult = model.classify(quick.view());
    const auto refinedResult = model.classify(refined.view());
    const auto repeatedResult = model.classify(refined.view());
    check(quickResult);
    check(refinedResult);
    check(repeatedResult);
    for (std::size_t i = 0; i < refinedResult.classes.size(); ++i) {
        const auto& first = refinedResult.classes[i];
        const auto& second = repeatedResult.classes[i];
        if (first.label != second.label ||
            std::abs(first.probability.value() -
                     second.probability.value()) > 1e-6)
            throw std::runtime_error("YAMNet inference is not repeatable");
    }

    std::ofstream out{outPath};
    if (!out) throw std::runtime_error("Cannot create YAMNet report");
    out << std::setprecision(9) << "model=YAMNet\n"
        << "input_frames=" << wav.audio.frameCount() << '\n'
        << "quick_frames=" << quick.frameCount() << '\n'
        << "refined_frames=" << refined.frameCount() << '\n';
    for (std::size_t i = 0; i < 5; ++i) {
        out << "quick_" << i << '=' << quickResult.classes[i].label
            << '\t' << quickResult.classes[i].probability.value() << '\n';
        out << "refined_" << i << '=' << refinedResult.classes[i].label
            << '\t' << refinedResult.classes[i].probability.value() << '\n';
    }
    if (!out) throw std::runtime_error("Cannot flush YAMNet report");
    std::cout << "real-yamnet-wav=ok quick=15600 refined=80000\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "Usage: real_yamnet_wav_replay MODEL LABELS WAV REPORT\n";
        return 2;
    }
    try {
        return run(argv[1], argv[2], argv[3], argv[4]);
    } catch (const std::exception& e) {
        std::cerr << "real-yamnet-wav=fail: " << e.what() << '\n';
        return 1;
    }
}

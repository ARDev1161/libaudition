// Real GTCRN ONNX inference through the public offline AND streaming adapters.
// The noise mixture is reproducible, not a claim of general speech quality.
#include <audition/backends/sherpa.hpp>
#include <audition/audio/audio_buffer.hpp>
#include <audition/io/wav_io.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

double meanSquaredError(const std::vector<float>& a,
                        const std::vector<float>& b) {
    require(!a.empty() && a.size() == b.size(), "MSE sample count mismatch");
    double accumulated = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double delta = static_cast<double>(a[i]) -
                             static_cast<double>(b[i]);
        accumulated += delta * delta;
    }
    return accumulated / static_cast<double>(a.size());
}

void validateOutput(const audition::AudioBuffer& output,
                    audition::AudioFormat expected) {
    require(output.format().sample_rate_hz == expected.sample_rate_hz &&
            output.format().channel_count == expected.channel_count,
            "GTCRN changed output sample rate or channel count");
    for (const float value : output.samples()) {
        require(std::isfinite(value), "GTCRN emitted a non-finite sample");
    }
}

void requireFlushedRejection(audition::INoiseSuppressorSession& session,
                             audition::AudioView frame) {
    bool rejected = false;
    try {
        static_cast<void>(session.process(frame));
    } catch (const audition::Error&) {
        rejected = true;
    }
    require(rejected, "GTCRN session accepted audio after flush");
}

void run(const std::string& modelPath, const std::string& wavPath,
         const std::string& reportPath) {
    require(std::filesystem::is_regular_file(modelPath), "Missing GTCRN ONNX model");
    const auto loaded = audition::io::loadWav(wavPath);
    const auto& clean = loaded.audio;
    const auto format = clean.format();
    require(format.sample_rate_hz == 16000U && format.channel_count == 1U &&
            format.layout == audition::AudioLayout::Interleaved,
            "GTCRN fixture must be mono 16-kHz interleaved PCM");
    require(clean.frameCount() >= 16000U, "GTCRN fixture is shorter than one second");

    // Xorshift32 generates deterministic full-band contamination.
    std::uint32_t state = 0x196AE09BU;
    std::vector<float> mixed = clean.samples();
    for (auto& sample : mixed) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        const float normalized = static_cast<float>(state & 0xFFFFU) /
                                 32767.5F - 1.0F;
        sample = std::clamp(sample + 0.065F * normalized, -1.0F, 1.0F);
    }
    const auto inputMse = meanSquaredError(mixed, clean.samples());
    require(inputMse > 0.0001, "Noise fixture was not generated");
    audition::AudioBuffer noisy{std::move(mixed), format, clean.captureTime(), 73U};

    audition::SherpaSpeechDenoiserOptions options{};
    std::get<audition::SherpaSpeechDenoiserGtcrnModel>(options.model).model = modelPath;

    audition::SherpaOfflineSpeechDenoiser offline{options};
    require(!offline.capabilities().streaming, "Offline GTCRN capability flag incorrect");
    auto session = offline.createSession(format);
    const auto offlineOutput = session->process(noisy.view());
    validateOutput(offlineOutput, format);
    require(offlineOutput.frameCount() == noisy.frameCount(),
            "Offline GTCRN returned unexpected sample count");
    require(offlineOutput.sequenceNumber() == 73U,
            "Offline GTCRN lost input sequence number");
    const auto offlineMse = meanSquaredError(offlineOutput.samples(), clean.samples());
    require(!session->flush().frameCount(), "Offline GTCRN flush returned unexpected audio");
    requireFlushedRejection(*session, noisy.view());
    session->reset();
    validateOutput(session->process(noisy.view()), format);

    audition::SherpaStreamingSpeechDenoiser streaming{options};
    const auto capabilities = streaming.capabilities();
    require(capabilities.streaming, "Streaming GTCRN capability flag incorrect");
    require(capabilities.audio.preferred_frame_count.has_value(),
            "GTCRN did not declare its streaming frame shift");
    const auto frameSize = *capabilities.audio.preferred_frame_count;
    require(frameSize > 0U && frameSize <= 16000U,
            "GTCRN streaming frame shift is unreasonable");
    auto stream = streaming.createSession(format);
    std::vector<float> frame(frameSize, 0.0F);
    std::vector<float> streamSamples;
    for (std::size_t offset = 0U; offset < noisy.samples().size(); offset += frameSize) {
        const auto remaining = noisy.samples().size() - offset;
        const auto count = std::min(frameSize, remaining);
        std::fill(frame.begin(), frame.end(), 0.0F);
        std::copy_n(noisy.samples().data() + offset, count, frame.data());
        const audition::AudioView view{
            frame.data(), frame.size(), format, clean.captureTime(),
            static_cast<std::uint64_t>(offset / frameSize + 1U)};
        const auto output = stream->process(view);
        validateOutput(output, format);
        streamSamples.insert(streamSamples.end(),
                             output.samples().begin(), output.samples().end());
    }
    const auto tail = stream->flush();
    validateOutput(tail, format);
    streamSamples.insert(streamSamples.end(), tail.samples().begin(),
                         tail.samples().end());
    require(streamSamples.size() >= frameSize,
            "Streaming GTCRN did not emit enhanced audio");
    const audition::AudioView firstFrame{
        noisy.samples().data(), frameSize, format, clean.captureTime(), 1U};
    requireFlushedRejection(*stream, firstFrame);
    stream->reset();
    validateOutput(stream->process(firstFrame), format);

    std::ofstream report{reportPath};
    require(report.good(), "Cannot write GTCRN integration report");
    report << "model=gtcrn_simple.onnx\n"
           << "fixture_samples=" << clean.frameCount() << '\n'
           << "noise_mse=" << inputMse << '\n'
           << "offline_samples=" << offlineOutput.frameCount() << '\n'
           << "offline_mse=" << offlineMse << '\n'
           << "stream_frame_shift=" << frameSize << '\n'
           << "stream_samples=" << streamSamples.size() << '\n';
    require(report.good(), "Failed to write GTCRN integration report");
    std::cout << "PASS: GTCRN real ONNX offline and streaming sessions" << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: real_gtcrn_denoise model.onnx speech.wav report.txt\n";
        return 2;
    }
    try {
        run(argv[1], argv[2], argv[3]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

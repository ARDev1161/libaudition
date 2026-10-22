// Genuine sherpa-onnx streaming Zipformer ASR on the upstream spoken WAV.
// Deliberately separate from offline Whisper and from KWS inference.
#include <audition/backends/sherpa.hpp>
#include <audition/io/wav_io.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool yes, const char* why) {
    if (!yes) throw std::runtime_error{why};
}

std::string normalized(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](unsigned char c) { return std::isspace(c) != 0; }),
            s.end());
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

struct Result {
    std::string text;
    std::size_t partials_with_text{0};
    bool endpoint_queried{false};
};

Result replay(audition::IStreamingAsrSession& session,
              const audition::AudioBuffer& audio) {
    const auto format = audio.format();
    constexpr std::size_t kChunk = 1600U;  // 100 ms at 16 kHz
    Result result{};
    for (std::size_t offset = 0; offset < audio.samples().size(); offset += kChunk) {
        const auto count = std::min(kChunk, audio.samples().size() - offset);
        const audition::AudioView chunk{
            audio.samples().data() + offset, count, format, audio.captureTime(),
            static_cast<std::uint64_t>(offset / kChunk)};
        session.accept(chunk);
        const auto partial = session.partial();
        if (!partial.text.empty()) ++result.partials_with_text;
        static_cast<void>(session.endpointDetected());
        result.endpoint_queried = true;
    }
    // The online recognizer may need post-roll to decode the last tokens.
    std::vector<float> silence(6400U, 0.0F);
    session.accept({silence.data(), silence.size(), format, audio.captureTime()});
    const auto final = session.finalize();
    result.text = final.text;
    require(session.finalize().text == final.text,
            "Streaming ASR second finalize returned a different transcript");
    require(session.partial().text == final.text,
            "Streaming ASR partial() after finalize disagrees with final transcript");

    bool rejected = false;
    try {
        session.accept({silence.data(), kChunk, format, audio.captureTime()});
    } catch (const audition::Error& error) {
        rejected = error.code() == audition::ErrorCode::InvalidState;
    }
    require(rejected, "Streaming ASR accepted audio after finalize without reset");
    return result;
}

void run(const std::filesystem::path& model,
         const std::filesystem::path& sample,
         const std::filesystem::path& reportPath) {
    const auto stem = std::string{"epoch-99-avg-1"};
    const auto encoder = model / ("encoder-" + stem + ".int8.onnx");
    const auto decoder = model / ("decoder-" + stem + ".onnx");
    const auto joiner = model / ("joiner-" + stem + ".int8.onnx");
    const auto tokens = model / "tokens.txt";
    for (const auto& file : {encoder, decoder, joiner, tokens, sample}) {
        require(std::filesystem::is_regular_file(file), "Missing Streaming ASR model/WAV fixture");
        require(std::filesystem::file_size(file) > 0U,
                "Empty Streaming ASR model/WAV fixture");
    }
    const auto audio = audition::io::loadWav(sample).audio;
    const auto format = audio.format();
    require(format.channel_count == 1U &&
            format.sample_rate_hz == 16000U &&
            format.layout == audition::AudioLayout::Interleaved,
            "Streaming ASR fixture must be 16 kHz mono interleaved");
    require(audio.frameCount() > 16000U, "Streaming ASR test WAV too short");

    audition::SherpaStreamingAsrOptions options{};
    options.model = audition::SherpaOnlineTransducerModel{encoder, decoder, joiner};
    options.tokens = tokens;
    options.runtime.num_threads = 1;
    options.enable_endpoint = true;
    audition::SherpaStreamingAsr recognizer{options};
    const auto capabilities = recognizer.capabilities();
    require(capabilities.partial_results && capabilities.endpoint_detection,
            "Sherpa Streaming ASR advertised incorrect capabilities");
    require(!capabilities.audio.supported_sample_rates_hz.empty() &&
            capabilities.audio.supported_sample_rates_hz.front() == 16000U,
            "Sherpa Streaming ASR advertised wrong input rate");

    auto session = recognizer.createSession();
    const auto first = replay(*session, audio);
    const auto text = normalized(first.text);
    require(text.find("LIGHTUP") != std::string::npos,
            "Streaming ASR failed to transcribe LIGHT UP from real upstream speech");
    require(first.partials_with_text > 0U && first.endpoint_queried,
            "Streaming ASR never returned a nonempty incremental transcript");

    session->reset();
    const auto second = replay(*session, audio);
    require(normalized(second.text).find("LIGHTUP") != std::string::npos,
            "Streaming ASR reset did not restore recognition");
    require(second.partials_with_text > 0U,
            "Streaming ASR after reset did not produce partial results");

    // Ensure a second logical session does not share the first session's state.
    auto independent = recognizer.createSession();
    require(normalized(replay(*independent, audio).text).find("LIGHTUP") != std::string::npos,
            "Independent Streaming ASR session failed to transcribe known phrase");

    std::ofstream report{reportPath};
    require(report.good(), "Cannot open Streaming ASR result report");
    report << "model=streaming-zipformer-en-20M-2023-02-17-mobile\n"
           << "fixture_samples=" << audio.frameCount() << '\n'
           << "first_transcript=" << first.text << '\n'
           << "reset_transcript=" << second.text << '\n'
           << "first_nonempty_partials=" << first.partials_with_text << '\n'
           << "reset_nonempty_partials=" << second.partials_with_text << '\n';
    require(report.good(), "Could not write Streaming ASR result report");
    std::cout << "PASS: real Streaming Zipformer INT8 incremental ASR, "
              << "finalize, reset, multi-session; partial updates="
              << first.partials_with_text << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: sherpa_real_streaming_asr MODEL_DIR WAV REPORT\n";
        return 2;
    }
    try {
        run(argv[1], argv[2], argv[3]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: real Sherpa Streaming ASR: " << error.what() << '\n';
        return 1;
    }
}

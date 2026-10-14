// Real English keyword spotting against the upstream GigaSpeech Zipformer
// model's attributed positive WAV fixture (no synthetic recognizer or mock).
#include <audition/backends/sherpa.hpp>
#include <audition/io/wav_io.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool valid, const char* message) {
    if (!valid) throw std::runtime_error{message};
}

std::string normalized(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](unsigned char c) { return std::isspace(c) != 0; }),
            s.end());
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

void verify(const std::filesystem::path& modelDir) {
    const auto suffix = "epoch-12-avg-2-chunk-16-left-64.int8.onnx";
    const auto encoder = modelDir / (std::string{"encoder-"} + suffix);
    const auto decoder = modelDir / (std::string{"decoder-"} + suffix);
    const auto joiner = modelDir / (std::string{"joiner-"} + suffix);
    const auto tokens = modelDir / "tokens.txt";
    const auto keywords = modelDir / "test_wavs" / "test_keywords.txt";
    const auto wav = modelDir / "test_wavs" / "0.wav";

    for (const auto& path : {encoder, decoder, joiner, tokens, keywords, wav}) {
        require(std::filesystem::is_regular_file(path), "KWS model fixture file is missing");
    }

    audition::SherpaKeywordSpotterOptions options{};
    options.model = audition::SherpaOnlineTransducerModel{encoder, decoder, joiner};
    options.tokens = tokens;
    options.keywords_file = keywords;
    options.runtime.num_threads = 1;
    audition::SherpaKeywordSpotter engine{options};
    const auto requirements = engine.audioRequirements();

    const auto loaded = audition::io::loadWav(wav);
    const auto& audio = loaded.audio;
    const auto format = audio.format();
    require(format.channel_count == 1U &&
            format.layout == audition::AudioLayout::Interleaved &&
            format.sample_rate_hz == 16000U,
            "KWS upstream test WAV must be mono 16-kHz");
    require(audio.frameCount() >= 16000U,
            "KWS upstream positive WAV is unexpectedly short");
    require(!requirements.supported_sample_rates_hz.empty() &&
            requirements.supported_sample_rates_hz.front() == format.sample_rate_hz,
            "KWS advertises an unexpected audio sample rate");

    auto session = engine.createSession();
    constexpr std::size_t chunk = 2560U;
    std::vector<audition::KeywordHit> hits;
    auto feed = [&](const float* samples, std::size_t count) {
        const audition::AudioView view{samples, count, format, audio.captureTime()};
        auto found = session->process(view);
        hits.insert(hits.end(), found.begin(), found.end());
    };
    for (std::size_t offset = 0; offset < audio.samples().size(); offset += chunk) {
        feed(audio.samples().data() + offset,
             std::min(chunk, audio.samples().size() - offset));
    }
    // Trigger the decoder on any remaining buffered frames after the WAV.
    std::vector<float> tail(16000U, 0.0F);
    for (std::size_t offset = 0; offset < tail.size(); offset += chunk) {
        feed(tail.data() + offset, std::min(chunk, tail.size() - offset));
    }
    const auto positive = std::find_if(hits.begin(), hits.end(),
        [](const audition::KeywordHit& hit) {
            return normalized(hit.keyword) == "LIGHTUP";
        });
    require(positive != hits.end(),
            "Real KWS did not detect expected LIGHT UP in upstream positive WAV");
    require(positive->offset.nanoseconds() >= 0 &&
            positive->offset.seconds() <= audio.duration().seconds() + 1.0,
            "KWS returned an invalid keyword timestamp");

    session->reset();
    std::vector<float> silence(16000U, 0.0F);
    std::size_t spurious = 0U;
    for (std::size_t offset = 0; offset < silence.size(); offset += chunk) {
        const auto view = audition::AudioView{
            silence.data() + offset, std::min(chunk, silence.size() - offset),
            format, audio.captureTime()};
        spurious += session->process(view).size();
    }
    require(spurious == 0U, "KWS emitted a keyword for one second of silence");
    std::cout << "PASS KWS real GigaSpeech Zipformer INT8: LIGHT UP detected, "
              << hits.size() << " hits, silence hits=" << spurious << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: sherpa_real_kws <model-dir>\n";
        return 2;
    }
    try {
        verify(argv[1]);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL sherpa_real_kws: " << exception.what() << '\n';
        return 1;
    }
}

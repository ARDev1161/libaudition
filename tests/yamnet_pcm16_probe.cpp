#include <audition/backends/yamnet/audio_tagger.hpp>
#include <audition/audio/audio_buffer.hpp>

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: yamnet_pcm16_probe MODEL.onnx LABELS.csv MONO16K.raw\n";
            return 2;
        }
        audition::YamnetOnnxOptions options{};
        options.model = argv[1];
        options.labels = argv[2];
        options.top_k = 10U;
        const audition::YamnetAudioTagger model{options};
        const auto expected = model.capabilities().audio.preferred_frame_count.value_or(15600U);
        std::ifstream input{argv[3], std::ios::binary};
        if (!input) throw std::runtime_error{"Cannot read PCM16 input"};
        const std::vector<unsigned char> pcm{
            std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        if (pcm.size() != expected * 2U) {
            throw std::runtime_error{"PCM16 length differs from model preferred frame count"};
        }
        std::vector<float> samples(expected);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto lo = static_cast<std::uint16_t>(pcm[2U*i]);
            const auto hi = static_cast<std::uint16_t>(pcm[2U*i+1U]);
            const auto value = static_cast<std::int16_t>(lo | (hi << 8U));
            samples[i] = static_cast<float>(value) / 32768.0F;
        }
        audition::AudioBuffer audio{std::move(samples),
            {16000U,1U,audition::AudioLayout::Interleaved}, audition::Timestamp{},0U};
        const auto result = model.classify(audio.view());
        if (result.classes.size() != 10U) throw std::runtime_error{"Expected 10 classes"};
        std::cout << std::fixed << std::setprecision(9);
        for (const auto& c : result.classes) {
            std::cout << c.label << "\t" << c.probability.value() << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "YAMNet probe failed: " << e.what() << '\n';
        return 1;
    }
}

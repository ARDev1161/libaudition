#include <audition/backends/efficientat/audio_tagger.hpp>
#include <audition/audio/audio_buffer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint32_t u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8U) |
        (static_cast<std::uint32_t>(p[2]) << 16U) |
        (static_cast<std::uint32_t>(p[3]) << 24U);
}
std::uint16_t u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8U);
}
std::vector<float> loadWav(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open WAV");
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < 44 || std::string(reinterpret_cast<const char*>(bytes.data()), 4) != "RIFF" ||
        std::string(reinterpret_cast<const char*>(bytes.data() + 8), 4) != "WAVE") {
        throw std::runtime_error("Expected RIFF/WAVE");
    }
    std::uint16_t fmt = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    std::size_t data_offset = 0, data_bytes = 0;
    for (std::size_t i = 12; i + 8 <= bytes.size();) {
        const std::size_t length = u32(bytes.data() + i + 4);
        const std::size_t start = i + 8;
        if (length > bytes.size() - start) throw std::runtime_error("Malformed WAV chunk");
        const std::string kind(reinterpret_cast<const char*>(bytes.data() + i), 4);
        if (kind == "fmt ") {
            if (length < 16) throw std::runtime_error("Bad WAV fmt");
            fmt = u16(bytes.data() + start);
            channels = u16(bytes.data() + start + 2);
            rate = u32(bytes.data() + start + 4);
            bits = u16(bytes.data() + start + 14);
        } else if (kind == "data") {
            data_offset = start;
            data_bytes = length;
        }
        i = start + length + (length & 1U);
    }
    if (fmt != 1 || bits != 16 || channels != 1 || rate != 32000 ||
        data_bytes == 0 || data_bytes % 2 != 0 || data_bytes > 32000U * 2U * 20U) {
        throw std::runtime_error("Benchmark requires mono PCM16 WAV at 32000 Hz, <=20 seconds");
    }
    std::vector<float> samples(data_bytes / 2);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto raw = static_cast<std::int16_t>(u16(bytes.data() + data_offset + i * 2));
        samples[i] = static_cast<float>(raw) / 32768.0F;
    }
    return samples;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: efficientat_wav_benchmark MODEL.onnx LABELS.txt 32k_mono_pcm16.wav\n";
            return 2;
        }
        audition::EfficientAtOnnxOptions cfg;
        cfg.model = argv[1];
        cfg.labels = argv[2];
        cfg.top_k = 5;
        const audition::EfficientAtAudioTagger tagger{cfg};
        const auto samples = loadWav(argv[3]);
        const bool mn10 = std::string(argv[1]).find("mn10_as.onnx") != std::string::npos;
        const std::size_t required = mn10 ? 160000U : 32000U;
        if (samples.size() < required) throw std::runtime_error("WAV shorter than required model window");
        std::vector<float> window(samples.begin(), samples.begin() + required);
        audition::AudioBuffer audio{std::move(window),
            {32000U, 1U, audition::AudioLayout::Interleaved},
            audition::Timestamp{}, 0U};

        const auto warmup = tagger.classify(audio.view());
        if (warmup.classes.empty()) throw std::runtime_error("Empty classification result");
        std::vector<double> measurements;
        for (int i = 0; i < 20; ++i) {
            const auto start = std::chrono::steady_clock::now();
            const auto result = tagger.classify(audio.view());
            const auto end = std::chrono::steady_clock::now();
            if (result.classes.empty()) throw std::runtime_error("Empty classification");
            measurements.push_back(std::chrono::duration<double, std::milli>(end-start).count());
        }
        std::sort(measurements.begin(), measurements.end());
        const auto percentile = [&](double q) {
            const std::size_t index = static_cast<std::size_t>(q * static_cast<double>(measurements.size()-1));
            return measurements[index];
        };
        std::cout << std::fixed << std::setprecision(4)
                  << "median_ms=" << percentile(0.5)
                  << " p95_ms=" << percentile(0.95)
                  << " min_ms=" << measurements.front()
                  << " max_ms=" << measurements.back()
                  << " rtf=" << percentile(0.5) / (required / 32.0) << "\n";
        for (const auto& c : warmup.classes) {
            std::cout << c.label << " " << c.probability.value() << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

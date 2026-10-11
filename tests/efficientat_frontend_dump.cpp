#include <audition/backends/efficientat/frontend.hpp>
#include <audition/audio/audio_buffer.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3) {
            std::cerr << "usage: efficientat_frontend_dump output.f32 [32k_mono_pcm16.wav]\n";
            return 2;
        }
        // Deterministic multi-frequency + chirp-like input; not a trivial
        // silence fixture. Keep exactly synchronized with parity script.
        std::vector<float> samples(32000U);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const double t = static_cast<double>(i) / 32000.0;
            samples[i] = static_cast<float>(
                0.1 * std::sin(2.0 * 3.141592653589793 * 440.0 * t) +
                0.05 * std::cos(2.0 * 3.141592653589793 * 1730.0 * t) +
                0.02 * std::sin(2.0 * 3.141592653589793 * (250.0 * t + 300.0 * t * t)));
        }
        if (argc == 3) {
            std::ifstream wav{argv[2], std::ios::binary};
            const std::vector<unsigned char> bytes{
                std::istreambuf_iterator<char>{wav}, std::istreambuf_iterator<char>{}};
            if (bytes.size() != 64044U && bytes.size() != 320044U) {
                throw std::runtime_error{"Expected exact 1s or 5s WAV"};
            }
            if (
                std::string(reinterpret_cast<const char*>(bytes.data()),4) != "RIFF" ||
                std::string(reinterpret_cast<const char*>(bytes.data()+8),4) != "WAVE" ||
                std::string(reinterpret_cast<const char*>(bytes.data()+12),4) != "fmt " ||
                std::string(reinterpret_cast<const char*>(bytes.data()+36),4) != "data" ||
                bytes[22] != 1 || bytes[23] != 0 || bytes[34] != 16 ||
                bytes[24] != 0 || bytes[25] != 125) {
                throw std::runtime_error{"Expected canonical PCM16 32k mono WAV"};
            }
            samples.resize((bytes.size() - 44U) / 2U);
            for (std::size_t i=0; i<samples.size(); ++i) {
                const auto lo = static_cast<std::uint16_t>(bytes[44+i*2]);
                const auto hi = static_cast<std::uint16_t>(bytes[45+i*2]);
                const auto code = static_cast<std::int16_t>(lo | (hi << 8U));
                samples[i] = static_cast<float>(code) / 32768.0F;
            }
        }
        audition::AudioBuffer audio{std::move(samples),
            {32000U, 1U, audition::AudioLayout::Interleaved},
            audition::Timestamp{}, 0U};
        const auto mel = audition::EfficientAtWaveformFrontend{}.compute(audio.view());
        if (mel.mel_bins != 128U || mel.frames != (samples.size() / 320U)) {
            throw std::runtime_error{"Unexpected mel tensor shape"};
        }
        std::ofstream out{argv[1], std::ios::binary};
        if (!out) throw std::runtime_error{"Failed to open output"};
        out.write(reinterpret_cast<const char*>(mel.values.data()),
                  static_cast<std::streamsize>(mel.values.size() * sizeof(float)));
        if (!out) throw std::runtime_error{"Failed to write output"};
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

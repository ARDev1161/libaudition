#include <audition/backends/efficientat/frontend.hpp>
#include <audition/audio/audio_buffer.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "usage: efficientat_frontend_dump output.f32\n";
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
        audition::AudioBuffer audio{std::move(samples),
            {32000U, 1U, audition::AudioLayout::Interleaved},
            audition::Timestamp{}, 0U};
        const auto mel = audition::EfficientAtWaveformFrontend{}.compute(audio.view());
        if (mel.mel_bins != 128U || mel.frames != 100U) {
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

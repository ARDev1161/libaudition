#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace demo {
struct NormalizedWav {
    std::filesystem::path path;
    double gain_db{0};
};
// Only processes the demo recorder's PCM16 mono RIFF/WAV (44-byte header).
// Writes a separate temporary normalized file; source WAV is never modified.
inline NormalizedWav normalizePlaybackWav(const std::filesystem::path& source) {
    std::ifstream in(source, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open source recording");
    char header[44]{};
    in.read(header, 44);
    if (in.gcount() != 44 || std::string(header, 4) != "RIFF" ||
        std::string(header + 8, 4) != "WAVE" ||
        std::string(header + 36, 4) != "data" ||
        static_cast<unsigned char>(header[20]) != 1 ||
        static_cast<unsigned char>(header[22]) != 1 ||
        static_cast<unsigned char>(header[34]) != 16)
        throw std::runtime_error("Unsupported WAV layout for normalization");
    auto get16 = [](const char* p) {
        return static_cast<std::uint16_t>(
            static_cast<unsigned char>(p[0]) |
            (static_cast<unsigned>(static_cast<unsigned char>(p[1])) << 8));
    };
    const auto bytes = std::filesystem::file_size(source);
    if (bytes < 44) throw std::runtime_error("Invalid WAV size");
    // Scan the complete payload, including recent samples not yet reflected in
    // an actively recorded file's RIFF header.
    std::uint32_t maxPeak = 0;
    char pair[2];
    while (in.read(pair, 2)) {
        const auto raw = static_cast<std::int16_t>(get16(pair));
        const auto amplitude = static_cast<std::uint32_t>(std::abs(static_cast<int>(raw)));
        maxPeak = std::max(maxPeak, amplitude);
    }
    const double gain = maxPeak == 0 ? 1.0 :
        std::max(1.0, std::min(100.0, (32767.0 * std::pow(10.0, -3.0 / 20.0)) /
                                 static_cast<double>(maxPeak)));
    const double db = 20.0 * std::log10(gain);
    auto dest = std::filesystem::temp_directory_path() /
        ("libaudition-playback-" + source.filename().string());
    // Source basename may collide across capture sessions. Include parent session.
    dest = std::filesystem::temp_directory_path() /
        ("libaudition-playback-" + source.parent_path().filename().string() +
         "-" + source.filename().string());
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot create normalized recording");
    in.clear(); in.seekg(0);
    out.write(header, 44);
    in.seekg(44);
    while (in.read(pair, 2)) {
        const auto raw = static_cast<std::int16_t>(get16(pair));
        const auto scaled = static_cast<int>(std::lrint(static_cast<double>(raw) * gain));
        const auto sample = static_cast<std::uint16_t>(
            static_cast<std::int16_t>(std::clamp(scaled, -32768, 32767)));
        const char data[2]{static_cast<char>(sample & 255),
                           static_cast<char>((sample >> 8) & 255)};
        out.write(data, 2);
    }
    if (!out) throw std::runtime_error("Cannot write normalized recording");
    const auto dataBytes = static_cast<std::uint32_t>(std::filesystem::file_size(dest) - 44);
    auto put32 = [](std::ostream& f, std::uint32_t x) {
        for (int i = 0; i < 4; ++i) f.put(static_cast<char>((x >> (i * 8)) & 255));
    };
    out.seekp(4); put32(out, dataBytes + 36);
    out.seekp(40); put32(out, dataBytes);
    out.close();
    return {dest, db};
}
} // namespace demo

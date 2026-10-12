#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace demo {

// Demo-only disk recorder. Each ODAS identity gets its own mono PCM16 WAV.
// A global cap prevents uncontrolled growth when ODAS rapidly creates IDs.
// Non-finite input is sanitized. I/O failures disable recording, never capture.
class SourceWavRecorder {
public:
    struct Entry {
        std::string path;
        std::uint64_t samples{0};
        bool active{false};
    };
    SourceWavRecorder(std::filesystem::path directory, std::uint32_t rate,
                      std::uint64_t budget_bytes = 256ULL * 1024ULL * 1024ULL)
        : directory_(std::move(directory)), rate_(rate), budget_(budget_bytes) {
        std::error_code ec;
        std::filesystem::create_directories(directory_, ec);
        enabled_ = !ec && rate_ != 0;
    }
    ~SourceWavRecorder() { closeAll(); }

    void append(std::uint64_t id, const std::vector<float>& samples) noexcept {
        try {
            if (!enabled_ || samples.empty() || used_ >= budget_) return;
            auto it = streams_.find(id);
            if (it == streams_.end()) {
                const auto path = directory_ / ("track_" + std::to_string(id) +
                                                 "_" + std::to_string(++sequence_) + ".wav");
                Stream entry;
                entry.file.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
                if (!entry.file) return;
                entry.meta.path = path.string();
                entry.meta.active = true;
                writeHeader(entry);
                it = streams_.emplace(id, std::move(entry)).first;
            }
            auto& stream = it->second;
            const std::size_t count = static_cast<std::size_t>(
                std::min<std::uint64_t>(samples.size(), (budget_ - used_) / 2));
            for (std::size_t i = 0; i < count; ++i) {
                const double x = std::isfinite(samples[i]) ? samples[i] : 0.;
                const auto value = static_cast<std::int16_t>(
                    std::lrint(std::clamp(x, -1., 1.) * 32767.));
                const auto u = static_cast<std::uint16_t>(value);
                const char bytes[2] = {static_cast<char>(u & 255),
                                       static_cast<char>((u >> 8) & 255)};
                stream.file.write(bytes, 2);
            }
            if (!stream.file) { retire(id); return; }
            stream.meta.samples += count;
            used_ += 2 * count;
            // Update header for playback of still-active sources at ~1 s intervals.
            if (stream.meta.samples - stream.last_header_samples >= rate_) {
                writeHeader(stream);
                stream.last_header_samples = stream.meta.samples;
            }
        } catch (...) { enabled_ = false; }
    }

    void retire(std::uint64_t id) noexcept {
        const auto it = streams_.find(id);
        if (it == streams_.end()) return;
        try {
            writeHeader(it->second);
            it->second.file.close();
        } catch (...) {}
        auto m = it->second.meta;
        m.active = false;
        entries_[id] = std::move(m);
        streams_.erase(it);
    }
    void closeAll() noexcept {
        while (!streams_.empty()) retire(streams_.begin()->first);
    }
    [[nodiscard]] std::map<std::uint64_t, Entry> entries() const {
        auto out = entries_;
        for (const auto& item : streams_) out[item.first] = item.second.meta;
        return out;
    }
private:
    struct Stream {
        std::ofstream file;
        Entry meta;
        std::uint64_t last_header_samples{0};
    };
    static void u16(std::ofstream& f, std::uint16_t x) {
        const char b[2]{char(x & 255), char((x >> 8) & 255)};
        f.write(b, 2);
    }
    static void u32(std::ofstream& f, std::uint32_t x) {
        u16(f, static_cast<std::uint16_t>(x & 65535));
        u16(f, static_cast<std::uint16_t>(x >> 16));
    }
    void writeHeader(Stream& s) {
        // Standard RIFF/WAVE PCM16 mono. RIFF size must fit uint32.
        const auto bytes = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(s.meta.samples * 2, 0xffffffffULL - 36ULL));
        const auto pos = s.file.tellp();
        s.file.seekp(0);
        s.file.write("RIFF", 4); u32(s.file, 36 + bytes);
        s.file.write("WAVEfmt ", 8);
        u32(s.file, 16); u16(s.file, 1); u16(s.file, 1);
        u32(s.file, rate_); u32(s.file, rate_ * 2);
        u16(s.file, 2); u16(s.file, 16);
        s.file.write("data", 4); u32(s.file, bytes);
        s.file.seekp(pos);
        s.file.flush();
    }
    std::filesystem::path directory_;
    std::uint32_t rate_;
    std::uint64_t budget_;
    std::uint64_t used_{0}, sequence_{0};
    bool enabled_{false};
    std::map<std::uint64_t, Stream> streams_;
    std::map<std::uint64_t, Entry> entries_;
};

} // namespace demo

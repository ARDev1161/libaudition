#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace demo {
// Qt-demo recorder: append is bounded, non-blocking with respect to disk IO.
// Every retirement starts a new generation, even if ODAS reuses its numeric ID.
class SourceWavRecorder final {
public:
    struct Entry {
        std::uint64_t recording_id{0}, track_id{0}, samples{0};
        std::string path;
        bool active{false};
    };
    SourceWavRecorder(std::filesystem::path dir, std::uint32_t rate,
                      std::uint64_t budget = 256ULL * 1024 * 1024,
                      std::size_t queue_limit = 256)
        : dir_(std::move(dir)), rate_(rate), budget_(budget),
          queue_limit_(queue_limit) {
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        enabled_ = !ec && rate_ != 0 && queue_limit_ != 0;
        if (enabled_) worker_ = std::thread([this] { run(); });
    }
    ~SourceWavRecorder() { closeAll(); }
    SourceWavRecorder(const SourceWavRecorder&) = delete;
    SourceWavRecorder& operator=(const SourceWavRecorder&) = delete;

    void append(std::uint64_t track_id, const std::vector<float>& samples) noexcept {
        try {
            if (samples.empty()) return;
            std::lock_guard<std::mutex> lock(mu_);
            if (!enabled_ || stopping_ || queue_.size() >= queue_limit_ ||
                reserved_bytes_ >= budget_) { ++dropped_; return; }
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(samples.size(), (budget_ - reserved_bytes_) / 2));
            if (!count) { ++dropped_; return; }
            auto found = live_.find(track_id);
            if (found == live_.end()) {
                Entry meta;
                meta.track_id = track_id;
                meta.recording_id = ++next_id_;
                meta.path = (dir_ / ("track_" + std::to_string(track_id) + "_" +
                                     std::to_string(meta.recording_id) + ".wav")).string();
                meta.active = true;
                entries_[meta.recording_id] = meta;
                found = live_.emplace(track_id, meta.recording_id).first;
            }
            Command cmd;
            cmd.kind = Command::Audio;
            cmd.id = found->second;
            cmd.pcm.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                const double x = std::isfinite(samples[i]) ? samples[i] : 0.;
                cmd.pcm.push_back(static_cast<std::int16_t>(
                    std::lrint(std::clamp(x, -1., 1.) * 32767.)));
            }
            reserved_bytes_ += 2 * count;
            queue_.push_back(std::move(cmd));
            cv_.notify_one();
        } catch (...) { ++dropped_; }
    }
    void retire(std::uint64_t track_id) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = live_.find(track_id);
            if (it == live_.end()) return;
            auto id = it->second;
            live_.erase(it);
            entries_[id].active = false;
            queue_.push_back({Command::Close, id, {}});
            cv_.notify_one();
        } catch (...) { ++dropped_; }
    }
    void closeAll() noexcept {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stopping_) return;
            for (auto p : live_) {
                entries_[p.second].active = false;
                queue_.push_back({Command::Close, p.second, {}});
            }
            live_.clear();
            stopping_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
    [[nodiscard]] std::vector<Entry> entries() const {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<Entry> out;
        out.reserve(entries_.size());
        for (const auto& p : entries_) out.push_back(p.second);
        return out;
    }
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_.load(); }
private:
    struct Command {
        enum Kind { Audio, Close } kind{Audio};
        std::uint64_t id{0};
        std::vector<std::int16_t> pcm;
    };
    struct File { std::ofstream stream; std::uint64_t samples{0}; };
    static void u16(std::ostream& f, std::uint16_t v) {
        f.put(static_cast<char>(v)); f.put(static_cast<char>(v >> 8));
    }
    static void u32(std::ostream& f, std::uint32_t v) {
        u16(f, static_cast<std::uint16_t>(v));
        u16(f, static_cast<std::uint16_t>(v >> 16));
    }
    void header(File& f) {
        auto& out = f.stream;
        const auto pos = out.tellp();
        out.seekp(0);
        const auto bytes = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            f.samples * 2, 0xffffffffULL - 36));
        out.write("RIFF", 4); u32(out, 36 + bytes);
        out.write("WAVEfmt ", 8); u32(out, 16);
        u16(out, 1); u16(out, 1); u32(out, rate_); u32(out, rate_ * 2);
        u16(out, 2); u16(out, 16); out.write("data", 4); u32(out, bytes);
        out.seekp(pos < std::streampos(44) ? std::streampos(44) : pos);
        out.flush();
    }
    void run() noexcept {
        std::map<std::uint64_t, File> files;
        for (;;) {
            Command cmd;
            Entry meta;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) break;
                cmd = std::move(queue_.front());
                queue_.pop_front();
                meta = entries_[cmd.id];
            }
            try {
                if (cmd.kind == Command::Close) {
                    auto it = files.find(cmd.id);
                    if (it != files.end()) { header(it->second); files.erase(it); }
                    continue;
                }
                auto it = files.find(cmd.id);
                if (it == files.end()) {
                    File f;
                    f.stream.open(meta.path, std::ios::binary | std::ios::trunc);
                    if (!f.stream) { ++dropped_; continue; }
                    header(f);
                    it = files.emplace(cmd.id, std::move(f)).first;
                }
                auto& f = it->second;
                for (auto sample : cmd.pcm) u16(f.stream, static_cast<std::uint16_t>(sample));
                if (!f.stream) { ++dropped_; files.erase(it); continue; }
                f.samples += cmd.pcm.size();
                if (f.samples % rate_ < cmd.pcm.size()) header(f);
                std::lock_guard<std::mutex> lock(mu_);
                entries_[cmd.id].samples = f.samples;
            } catch (...) { ++dropped_; }
        }
        for (auto& p : files) { try { header(p.second); } catch (...) {} }
    }
    std::filesystem::path dir_;
    std::uint32_t rate_;
    std::uint64_t budget_, reserved_bytes_{0}, next_id_{0};
    std::size_t queue_limit_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Command> queue_;
    std::map<std::uint64_t, std::uint64_t> live_;
    std::map<std::uint64_t, Entry> entries_;
    std::thread worker_;
    std::atomic<std::uint64_t> dropped_{0};
    bool enabled_{false}, stopping_{false};
};
} // namespace demo

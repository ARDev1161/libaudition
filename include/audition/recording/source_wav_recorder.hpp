#pragma once
#include <algorithm>
#include <audition/io/wav_io.hpp>
#include <memory>
#include <set>
#include <array>
#include <limits>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace audition::recording {
// Qt-demo recorder: append is bounded, non-blocking with respect to disk IO.
// Every retirement starts a new generation, even if ODAS reuses its numeric ID.
class SourceWavRecorder final {
public:
    struct Entry {
        std::uint64_t recording_id{0}, track_id{0}, samples{0};
        std::uint64_t expected_samples{0}, lost_samples{0};
        std::string path;
        bool active{false};
    };
    struct AppendReport {
        std::uint64_t recording_id{0}, start_sample{0};
        std::uint64_t accepted_samples{0}, lost_samples{0};
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

    // Budget limits queued PCM bytes, NOT the length of the entire session.
    // Lost chunks consume the same logical sample timeline; the worker writes
    // zeros in their position so WAV and inference timestamps stay aligned.
    AppendReport append(
        std::uint64_t track_id, const std::vector<float>& samples) noexcept {
        AppendReport report{};
        if (samples.empty()) return report;
        try {
            std::lock_guard<std::mutex> lock(mu_);
            if (!enabled_ || stopping_) { ++dropped_; return report; }
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
            auto& entry = entries_.at(found->second);
            report.recording_id = entry.recording_id;
            report.start_sample = entry.expected_samples;
            const auto count = static_cast<std::uint64_t>(samples.size());
            // Guard classic RIFF size; caller must rotate tracks before 4 GiB.
            constexpr std::uint64_t maxSamples = (0xffffffffULL - 36ULL) / 2ULL;
            const auto remaining = maxSamples - std::min(entry.expected_samples, maxSamples);
            const auto requested = std::min(count, remaining);
            if (requested == 0) {
                report.lost_samples = count;
                ++dropped_;
                return report;
            }
            entry.expected_samples += requested;
            report.lost_samples = count - requested;
            if (pending_audio_ >= queue_limit_ ||
                requested > (budget_ - std::min(budget_, pending_bytes_)) / 2ULL) {
                report.lost_samples += requested;
                entry.lost_samples += requested;
                ++dropped_;
                return report;
            }
            Command cmd;
            cmd.kind = Command::Audio;
            cmd.id = entry.recording_id;
            cmd.start_sample = report.start_sample;
            cmd.pcm.reserve(static_cast<std::size_t>(requested));
            for (std::size_t i = 0; i < requested; ++i) {
                const double x = std::isfinite(samples[i]) ? samples[i] : 0.0;
                cmd.pcm.push_back(static_cast<std::int16_t>(
                    std::lrint(std::clamp(x, -1.0, 1.0) * 32767.0)));
            }
            queue_.push_back(std::move(cmd));
            pending_bytes_ += requested * 2ULL;
            report.accepted_samples = requested;
            if (report.lost_samples) { ++dropped_; }
            ++pending_audio_;
            cv_.notify_one();
        } catch (...) { ++dropped_; }
        return report;
    }
    void retire(std::uint64_t track_id) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = live_.find(track_id);
            if (it == live_.end()) return;
            auto id = it->second;
            live_.erase(it);
            entries_[id].active = false;
            queue_.push_back({Command::Close, id, 0, {}});
            cv_.notify_one();
        } catch (...) { ++dropped_; }
    }
    void closeAll() noexcept {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stopping_) return;
            for (auto p : live_) {
                entries_[p.second].active = false;
                queue_.push_back({Command::Close, p.second, 0, {}});
            }
            live_.clear();
            stopping_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }
    [[nodiscard]] std::optional<Entry> activeEntry(std::uint64_t track_id) const {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = live_.find(track_id);
        if (it == live_.end()) return std::nullopt;
        return entries_.at(it->second);
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
        std::uint64_t start_sample{0};
        std::vector<std::int16_t> pcm;
    };
    struct File {
        std::unique_ptr<audition::io::StreamingPcm16WavWriter> writer;
        std::uint64_t samples{0};
    };
    static void fillSilence(File& file, std::uint64_t count) {
        file.writer->appendSilence(count);
        file.samples = file.writer->sampleCount();
    }
    void run() noexcept {
        std::map<std::uint64_t, File> files;
        std::set<std::uint64_t> failedFiles;
        for (;;) {
            Command cmd;
            Entry meta;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) break;
                cmd = std::move(queue_.front());
                queue_.pop_front();
                if (cmd.kind == Command::Audio) {
                    --pending_audio_;
                    pending_bytes_ -= static_cast<std::uint64_t>(cmd.pcm.size()) * 2ULL;
                }
                meta = entries_[cmd.id];
            }
            try {
                if (failedFiles.count(cmd.id)) continue;
                const auto ensureFile = [&]() -> File& {
                    auto it = files.find(cmd.id);
                    if (it == files.end()) {
                        File file;
                        file.writer =
                            std::make_unique<audition::io::StreamingPcm16WavWriter>(
                                meta.path, rate_);
                        it = files.emplace(cmd.id, std::move(file)).first;
                    }
                    return it->second;
                };
                if (cmd.kind == Command::Close) {
                    auto& file = ensureFile();
                    if (meta.expected_samples > file.samples)
                        fillSilence(file, meta.expected_samples - file.samples);
                    file.writer->finish();
                    const auto written = file.samples;
                    files.erase(cmd.id);
                    std::lock_guard<std::mutex> lock(mu_);
                    entries_.at(cmd.id).samples = written;
                    continue;
                }
                auto& f = ensureFile();
                if (cmd.start_sample > f.samples)
                    fillSilence(f, cmd.start_sample - f.samples);
                f.writer->append(cmd.pcm.data(), cmd.pcm.size());
                f.samples = f.writer->sampleCount();
                std::lock_guard<std::mutex> lock(mu_);
                entries_[cmd.id].samples = f.samples;
            } catch (...) {
                ++dropped_;
                failedFiles.insert(cmd.id);
                // Do not reopen failed files with truncation on later chunks.
                files.erase(cmd.id);
            }
        }
        for (auto& p : files) {
            try { p.second.writer->finish(); } catch (...) { ++dropped_; }
        }
    }
    std::filesystem::path dir_;
    std::uint32_t rate_;
    std::uint64_t budget_, pending_bytes_{0}, next_id_{0};
    std::size_t queue_limit_;
    std::size_t pending_audio_{0};
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Command> queue_;
    std::map<std::uint64_t, std::uint64_t> live_;
    std::map<std::uint64_t, Entry> entries_;
    std::thread worker_;
    std::atomic<std::uint64_t> dropped_{0};
    bool enabled_{false}, stopping_{false};
};
} // namespace audition::recording

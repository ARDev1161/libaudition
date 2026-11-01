#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace demo {

// Capture-owner bookkeeping. Raw ASR logs and displayed transcripts belong to
// a recording ID, never an ODAS numeric track ID that can be reused while a
// retired generation is still completing ASR.
class SpeechRecordingLogRouter final {
public:
    void bind(std::uint64_t generation, std::uint64_t recording_id) {
        if (generation != 0U && recording_id != 0U)
            generations_[generation] = recording_id;
    }

    void unbind(std::uint64_t generation) noexcept {
        generations_.erase(generation);
    }

    [[nodiscard]] bool append(std::uint64_t generation,
                              const std::string& raw_line,
                              const std::string& transcript) {
        const auto it = generations_.find(generation);
        if (it == generations_.end()) return false;
        auto& log = logs_[it->second];
        log += raw_line;
        trim(log, 32768U, '\n');
        if (!transcript.empty()) {
            auto& text = transcripts_[it->second];
            if (!text.empty()) text += ' ';
            text += transcript;
            trim(text, 8192U, ' ');
        }
        return true;
    }

    // SourceWavRecorder already bounds active/completed history. Match that
    // lifetime rather than keeping unlimited ASR strings for retired tracks.
    void retain(const std::set<std::uint64_t>& recording_ids) {
        for (auto it = logs_.begin(); it != logs_.end();) {
            if (!recording_ids.count(it->first)) it = logs_.erase(it);
            else ++it;
        }
        for (auto it = transcripts_.begin(); it != transcripts_.end();) {
            if (!recording_ids.count(it->first)) it = transcripts_.erase(it);
            else ++it;
        }
    }

    [[nodiscard]] const std::map<std::uint64_t, std::string>&
    logs() const noexcept { return logs_; }
    [[nodiscard]] const std::map<std::uint64_t, std::string>&
    transcripts() const noexcept { return transcripts_; }

private:
    static void trim(std::string& s, std::size_t limit, char delimiter) {
        if (s.size() <= limit) return;
        const auto boundary = s.find(delimiter, s.size() - limit);
        s.erase(0, boundary == std::string::npos ?
                       s.size() - limit : boundary + 1U);
    }

    std::map<std::uint64_t, std::uint64_t> generations_{};
    std::map<std::uint64_t, std::string> logs_{};
    std::map<std::uint64_t, std::string> transcripts_{};
};

} // namespace demo

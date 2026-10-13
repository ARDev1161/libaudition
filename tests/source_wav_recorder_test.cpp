#include <audition/recording/source_wav_recorder.hpp>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static std::uint32_t le32(const char* p) {
    return static_cast<unsigned char>(p[0]) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}
int main() {
    namespace fs = std::filesystem;
    const auto path = fs::temp_directory_path() /
        ("libaudition-recorder-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        audition::recording::SourceWavRecorder recorder(path, 16000);
        const std::vector<float> data(1600, 0.25f);
        recorder.append(42, data);
        recorder.retire(42);
        recorder.append(42, data); // Same ODAS ID must create a NEW file.
        recorder.retire(42);
        recorder.closeAll();
        const auto all = recorder.entries();
        assert(all.size() == 2);
        assert(all[0].recording_id != all[1].recording_id);
        assert(all[0].track_id == 42 && all[1].track_id == 42);
        for (const auto& entry : all) {
            assert(!entry.active);
            assert(entry.samples == data.size());
            std::ifstream wav(entry.path, std::ios::binary);
            char head[44]{};
            wav.read(head, sizeof(head));
            assert(wav.gcount() == 44);
            assert(std::string(head, 4) == "RIFF");
            assert(std::string(head + 8, 4) == "WAVE");
            assert(std::string(head + 36, 4) == "data");
            assert(le32(head + 40) == data.size() * 2);
            assert(fs::file_size(entry.path) == 44 + data.size() * 2);
        }
    }
    {
        audition::recording::SourceWavRecorder recorder(path / "limited", 16000, 32);
        recorder.append(1, std::vector<float>(100, 0.1f));
        recorder.retire(1);
        recorder.closeAll();
        auto all = recorder.entries();
        assert(all.size() == 1);
        // Dropped audio is represented by silence, retaining 100 samples.
        assert(all[0].samples == 100);
        assert(all[0].expected_samples == 100);
    }
    std::error_code ec;
    fs::remove_all(path, ec);
}

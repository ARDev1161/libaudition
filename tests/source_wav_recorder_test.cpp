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
#include <thread>
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
    {
        audition::recording::SourceWavRecorder recorder(path / "gap", 16000, 32);
        const std::vector<float> shortAudio(8, 0.5f);
        const auto initial = recorder.append(9, shortAudio);
        assert(initial.accepted_samples == 8);
        for (int retry = 0; retry < 1000; ++retry) {
            const auto entries = recorder.entries();
            if (!entries.empty() && entries.front().samples == 8) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        const auto gap = recorder.append(9, std::vector<float>(100, 0.5f));
        assert(gap.lost_samples == 100);
        assert(gap.start_sample == 8);
        const auto after = recorder.append(9, shortAudio);
        assert(after.accepted_samples == 8);
        assert(after.start_sample == 108);
        recorder.retire(9);
        recorder.closeAll();
        const auto entries = recorder.entries();
        assert(entries.size() == 1);
        assert(entries.front().samples == 116);
        assert(entries.front().lost_samples == 100);
        std::ifstream wav(entries.front().path, std::ios::binary);
        wav.seekg(44);
        std::vector<char> pcm(116 * 2);
        wav.read(pcm.data(), static_cast<std::streamsize>(pcm.size()));
        assert(wav.good());
        const auto nonzero = [](char lo, char hi) {return lo != 0 || hi != 0;};
        assert(nonzero(pcm[0], pcm[1]));
        for (std::size_t i = 8; i < 108; ++i)
            assert(pcm[i * 2] == 0 && pcm[i * 2 + 1] == 0);
        assert(nonzero(pcm[108 * 2], pcm[108 * 2 + 1]));
    }

    {
        // Keep only the latest three completed entries, but do not delete
        // any physical WAV. Previously completed IDs must remain prunable.
        audition::recording::SourceWavRecorder recorder(
            path / "history",16000,4096,256,3);
        const std::vector<float> audio(16U,0.2F);
        std::vector<std::string> wav_paths;
        for (std::uint64_t i=1;i<=12U;++i) {
            const auto report=recorder.append(900U,audio);
            assert(report.recording_id==i);
            const auto entry=recorder.activeEntry(900U);
            assert(entry.has_value());
            wav_paths.push_back(entry->path);
            recorder.retire(900U);
        }
        recorder.closeAll();
        const auto recent=recorder.entries();
        assert(recent.size()==3U);
        assert(recent[0].recording_id==10U);
        assert(recent[1].recording_id==11U);
        assert(recent[2].recording_id==12U);
        assert(recorder.historyEvicted()==9U);
        for(const auto& wav:wav_paths) assert(fs::exists(wav));
    }
    {
        // A conflicting directory causes deterministic worker I/O failure.
        // Failed IDs must also be retired and bounded after Close.
        const auto fail_dir=path / "error-history";
        audition::recording::SourceWavRecorder recorder(
            fail_dir,16000,4096,256,2);
        for (std::uint64_t i=1;i<=6U;++i) {
            fs::create_directories(fail_dir /
                ("track_901_" + std::to_string(i) + ".wav"));
            recorder.append(901U,std::vector<float>(8U,0.2F));
            recorder.retire(901U);
        }
        recorder.closeAll();
        const auto recent=recorder.entries();
        assert(recent.size()==2U);
        assert(recent[0].recording_id==5U);
        assert(recent[1].recording_id==6U);
        assert(recorder.historyEvicted()==4U);
        for(const auto& entry:recent) assert(!entry.error.empty());
    }
    std::error_code ec;
    fs::remove_all(path, ec);
}

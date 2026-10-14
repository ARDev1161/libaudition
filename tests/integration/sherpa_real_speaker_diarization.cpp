// Real Pyannote + speaker-embedding offline diarization on a known upstream
// two-speaker English recording; no synthetic inference or stub recognizer.
#include <audition/backends/sherpa.hpp>
#include <audition/io/wav_io.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error{why};
}

void run(const std::filesystem::path& segmentation,
         const std::filesystem::path& embeddings,
         const std::filesystem::path& wav,
         const std::filesystem::path& reportPath) {
    for (const auto& file : {segmentation, embeddings, wav}) {
        require(std::filesystem::is_regular_file(file),
                "Missing real speaker-diarization model/WAV fixture");
        require(std::filesystem::file_size(file) > 0U,
                "Empty real speaker-diarization model/WAV fixture");
    }
    audition::SherpaSpeakerDiarizationOptions options{};
    options.segmentation_model = segmentation;
    options.embedding_model = embeddings;
    options.segmentation_runtime.num_threads = 1;
    options.embedding_runtime.num_threads = 1;
    options.num_clusters = 0;  // Let the model estimate number of speakers.
    audition::SherpaSpeakerDiarizer engine{options};

    const auto audio = audition::io::loadWav(wav).audio;
    const auto format = audio.format();
    require(format.sample_rate_hz == 16000U &&
            format.channel_count == 1U &&
            format.layout == audition::AudioLayout::Interleaved,
            "Diarization WAV fixture must be 16 kHz interleaved mono");
    require(audio.frameCount() >= 16000U,
            "Diarization reference WAV unexpectedly shorter than one second");
    const auto requirements = engine.audioRequirements();
    require(!requirements.supported_sample_rates_hz.empty() &&
            requirements.supported_sample_rates_hz.front() == 16000U,
            "Diarizer advertises incorrect audio requirements");

    const auto detected = engine.diarize(audio.view());
    const double duration = audio.duration().seconds();
    std::set<std::uint32_t> distinct;
    double totalSpeechSeconds = 0.0;
    for (const auto& segment : detected.segments) {
        const auto start = segment.start_offset.seconds();
        const auto end = segment.end_offset.seconds();
        require(std::isfinite(start) && std::isfinite(end) &&
                start >= 0.0 && end >= start && end <= duration + 0.5,
                "Diarizer returned invalid/out-of-bounds segment timestamps");
        require(segment.speaker_index < 100U,
                "Diarizer returned unreasonably large speaker index");
        if (segment.confidence.has_value()) {
            const auto value = segment.confidence->value;
            require(std::isfinite(value), "Diarizer returned nonfinite confidence");
        }
        distinct.insert(segment.speaker_index);
        totalSpeechSeconds += end - start;
    }
    require(detected.speaker_count == distinct.size(),
            "Diarization speaker_count inconsistent with emitted segments");
    require(detected.segments.size() >= 2U && distinct.size() == 2U,
            "Real two-speaker recording did not resolve exactly two speakers");
    require(totalSpeechSeconds >= 2.0,
            "Diarizer failed to recover meaningful speech duration");

    std::vector<float> invalid(800U, 0.0F);
    const audition::AudioView invalidRate{
        invalid.data(), invalid.size(),
        {8000U, 1U, audition::AudioLayout::Interleaved}, audio.captureTime()};
    bool rejected = false;
    try {
        static_cast<void>(engine.diarize(invalidRate));
    } catch (const audition::Error& error) {
        rejected = error.code() == audition::ErrorCode::UnsupportedFormat;
    }
    require(rejected, "Diarizer did not reject 8-kHz incompatible audio");

    std::ofstream report{reportPath};
    require(report.good(), "Cannot create speaker-diarization diagnostic report");
    report << "fixture=1-two-speakers-en.wav\n"
           << "samples=" << audio.frameCount() << '\n'
           << "speaker_count=" << detected.speaker_count << '\n'
           << "segment_count=" << detected.segments.size() << '\n'
           << "total_speech_seconds=" << totalSpeechSeconds << '\n'
           << "wrong_rate_rejected=" << (rejected ? 1 : 0) << '\n';
    for (const auto& segment : detected.segments) {
        report << "segment=" << segment.speaker_index << ','
               << segment.start_offset.seconds() << ','
               << segment.end_offset.seconds() << '\n';
    }
    require(report.good(), "Could not write diarization report");
    // Export the actual hypothesis, NOT an RTTM reference. An independent
    // human-annotated RTTM is required before any real DER can be claimed.
    // The RTTM recording ID is stable across the official pinned WAV fixture.
    auto rttmPath = reportPath;
    rttmPath.replace_extension(".rttm");
    std::ofstream rttm{rttmPath};
    require(rttm.good(), "Cannot create predicted speaker RTTM");
    rttm << std::fixed << std::setprecision(9);
    for (const auto& segment : detected.segments) {
        const auto start = segment.start_offset.seconds();
        const auto duration = segment.end_offset.seconds() - start;
        require(duration > 0.0, "Diarization produced a zero-duration segment");
        rttm << "SPEAKER 1-two-speakers-en 1 " << start << ' '
             << duration << " <NA> <NA> spk_" << segment.speaker_index
             << " <NA> <NA>\n";
    }
    require(rttm.good(), "Cannot write predicted speaker RTTM");
    std::cout << "PASS: real Pyannote speaker diarization split two labeled speakers ("
              << detected.segments.size() << " segments)\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: sherpa_real_speaker_diarization SEGMENT_MODEL EMBEDDING_MODEL WAV REPORT\n";
        return 2;
    }
    try {
        run(argv[1], argv[2], argv[3], argv[4]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: real Sherpa diarization: " << e.what() << '\n';
        return 1;
    }
}

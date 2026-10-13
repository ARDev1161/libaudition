// Offline integration driver: actual libaudition WAV -> ODAS SSL/SST/SSS ->
// per-source recorder + typed JSON sidecars, without ALSA, Qt or mock models.
// Metrics are written for a separate ground-truth validator, which can impose
// calibrated quality gates without hiding the raw trace.
#include <audition/audio/audio_buffer.hpp>
#include <audition/audio/recording_timeline_json.hpp>
#include <audition/backends/odas/odas_spatial_engine.hpp>
#include <audition/io/wav_io.hpp>
#include <audition/recording/source_recording_session.hpp>
#include <audition/recording/source_wav_recorder.hpp>

#include <algorithm>
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
namespace fs = std::filesystem;

audition::OdasOptions optionsForFixture() {
    audition::OdasOptions options{};
    options.microphone_array.microphones = {
        audition::MicrophoneGeometry{{-0.032, 0.0, 0.0}},
        audition::MicrophoneGeometry{{0.0, -0.032, 0.0}},
        audition::MicrophoneGeometry{{0.032, 0.0, 0.0}},
        audition::MicrophoneGeometry{{0.0, 0.032, 0.0}},
    };
    options.input_channels = {1U, 2U, 3U, 4U};
    options.sample_rate_hz = 16000U;
    options.hop_size = 128U;
    options.frame_size = 256U;
    options.speed_of_sound_mps = 343.0;
    // Keep production tracking thresholds: never special-case ODAS just to
    // make a synthetic fixture pass. Static SSL and SST are checked separately.
    options.sss.enabled = true;
    audition::validateOdasOptions(options);
    return options;
}

void validateDirection(const audition::Direction3D& direction) {
    const auto v = direction.vector();
    const auto len = v.squaredNorm();
    if (!std::isfinite(len) || std::abs(len - 1.0) > 0.0001)
        throw std::runtime_error("ODAS emitted non-unit or nonfinite direction");
}
void writeObservation(std::ostream& out, std::uint64_t hop,
                      const char* kind, std::int64_t id,
                      const audition::Direction3D& direction, double activity) {
    validateDirection(direction);
    if (!std::isfinite(activity) || activity < 0.0 || activity > 1.000001)
        throw std::runtime_error("ODAS emitted invalid activity/score");
    const auto v = direction.vector();
    out << hop << ',' << kind << ',' << id << ','
        << v.x << ',' << v.y << ',' << v.z << ','
        << activity << '\n';
    if (!out) throw std::runtime_error("Cannot write trace CSV");
}

int run(const fs::path& path, const fs::path& outputDir) {
    const auto wav = audition::io::loadWav(path);
    const auto format = wav.audio.format();
    const auto options = optionsForFixture();
    if (format.sample_rate_hz != options.sample_rate_hz ||
        format.channel_count != 6U ||
        format.layout != audition::AudioLayout::Interleaved ||
        wav.audio.frameCount() < options.hop_size ||
        wav.audio.frameCount() % options.hop_size != 0U)
        throw std::invalid_argument("Fixture must be 6-channel 16kHz with full ODAS hops");

    fs::create_directories(outputDir);
    std::ofstream trace{outputDir / "observations.csv", std::ios::trunc};
    if (!trace) throw std::runtime_error("Cannot create observations.csv");
    trace << "hop,kind,id,x,y,z,activity\n";
    trace << std::setprecision(10);

    audition::OdasSpatialEngine engine{options};
    audition::recording::SourceWavRecorder recorder{
        outputDir / "recordings", options.sample_rate_hz};
    audition::recording::SourceRecordingSession sessions{};
    std::set<std::uint64_t> seenTracks;
    const auto& pcm = wav.audio.samples();
    const auto hopSize = static_cast<std::size_t>(options.hop_size);
    const auto channels = static_cast<std::size_t>(format.channel_count);
    const auto hops = wav.audio.frameCount() / hopSize;
    std::uint64_t trackFrames = 0;
    std::uint64_t proposalFrames = 0;
    std::uint64_t separatedFrames = 0;

    for (std::size_t hop = 0; hop < hops; ++hop) {
        const auto begin = hop * hopSize * channels;
        std::vector<float> slice(
            pcm.begin() + static_cast<std::ptrdiff_t>(begin),
            pcm.begin() + static_cast<std::ptrdiff_t>(begin + hopSize * channels));
        const auto samplePos = hop * hopSize;
        const auto timeNs = static_cast<std::int64_t>(
            static_cast<std::uint64_t>(samplePos) * 1000000000ULL /
            static_cast<std::uint64_t>(format.sample_rate_hz));
        audition::AudioBuffer input{std::move(slice), format,
            audition::Timestamp{timeNs,
                {audition::ClockDomain::Simulation, 1U}},
            static_cast<std::uint64_t>(hop)};
        const auto result = engine.process(input.view());
        if (result.tracks.size() > options.sst.max_tracks ||
            result.potential_sources.size() > options.ssl.potential_source_count ||
            result.separated_frames.size() > result.tracks.size())
            throw std::runtime_error("ODAS exceeded configured bounded outputs");
        proposalFrames += result.potential_sources.size();
        for (const auto& p : result.potential_sources)
            writeObservation(trace, static_cast<std::uint64_t>(hop),
                             "proposal", -1, p.direction, p.score);
        trackFrames += result.tracks.size();
        std::set<std::uint64_t> current;
        for (const auto& track : result.tracks) {
            if (!current.insert(track.track_id.value()).second)
                throw std::runtime_error("Duplicate track ID in one ODAS hop");
            writeObservation(trace, static_cast<std::uint64_t>(hop), "track",
                static_cast<std::int64_t>(track.track_id.value()),
                track.direction.direction, track.activity.value());
        }
        for (const auto& frame : result.separated_frames) {
            const auto id = frame.track_id.value();
            if (!current.count(id) ||
                frame.audio.frameCount() != hopSize ||
                frame.audio.format().channel_count != 1U ||
                frame.audio.format().sample_rate_hz != options.sample_rate_hz)
                throw std::runtime_error("Invalid ODAS SSS frame/track association");
            for (const float sample : frame.audio.samples())
                if (!std::isfinite(sample))
                    throw std::runtime_error("Nonfinite ODAS SSS sample");
            ++separatedFrames;
            const auto written = recorder.append(id, frame.audio.samples());
            if (!written.recording_id)
                throw std::runtime_error("Recorder rejected source frame");
            if (!sessions.active(id)) {
                const auto entry = recorder.activeEntry(id);
                if (!entry || entry->recording_id != written.recording_id)
                    throw std::runtime_error("Invalid recorder generation mapping");
                sessions.begin(id, entry->recording_id, entry->path,
                               options.sample_rate_hz, frame.audio.captureTime());
            }
            if (written.lost_samples)
                sessions.addGap(id, written.start_sample +
                    written.accepted_samples, written.lost_samples);
            seenTracks.insert(id);
        }
    }

    for (const auto id : seenTracks) {
        recorder.retire(id);
        sessions.retire(id, std::nullopt);
    }
    recorder.closeAll();
    trace.close();

    const auto entries = recorder.entries();
    if (entries.size() != seenTracks.size())
        throw std::runtime_error("Recorder lost a tracked source");
    for (const auto& entry : entries) {
        if (!entry.error.empty())
            throw std::runtime_error("WAV recording error: " + entry.error);
        const auto recorded = audition::io::loadWav(entry.path);
        if (recorded.audio.frameCount() != entry.samples ||
            entry.samples != entry.expected_samples ||
            recorded.audio.format().channel_count != 1U)
            throw std::runtime_error("Recorded WAV sample alignment mismatch");
        const auto timeline = audition::readRecordingTimelineJson(
            fs::path{entry.path + ".json"});
        if (timeline.recordingId() != entry.recording_id ||
            timeline.sampleRateHz() != options.sample_rate_hz)
            throw std::runtime_error("Recording JSON identity mismatch");
        for (const auto& gap : timeline.gaps())
            if (gap.end_sample > entry.samples)
                throw std::runtime_error("Audio gap extends past WAV EOF");
    }
    if (sessions.persistenceError())
        throw std::runtime_error("JSON persistence: " +
                                 *sessions.persistenceError());

    std::ofstream summary{outputDir / "summary.json"};
    if (!summary) throw std::runtime_error("Cannot write summary.json");
    summary << "{\n"
        << "  \"schema\": \"libaudition.offline-integration.v1\",\n"
        << "  \"input_frames\": " << wav.audio.frameCount() << ",\n"
        << "  \"sample_rate_hz\": " << format.sample_rate_hz << ",\n"
        << "  \"hops\": " << hops << ",\n"
        << "  \"potential_observations\": " << proposalFrames << ",\n"
        << "  \"track_observations\": " << trackFrames << ",\n"
        << "  \"separated_frames\": " << separatedFrames << ",\n"
        << "  \"recordings\": " << entries.size() << ",\n"
        << "  \"recorder_drops\": " << recorder.dropped() << "\n"
        << "}\n";
    if (!summary) throw std::runtime_error("Cannot flush summary.json");
    std::cout << "offline-audio-integration=ok file=" << path.filename().string()
              << " hops=" << hops << " proposals=" << proposalFrames
              << " tracks=" << trackFrames << " sss=" << separatedFrames
              << " recordings=" << entries.size() << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: audition_offline_integration INPUT_6CH_WAV OUTPUT_DIRECTORY\n";
        return 2;
    }
    try {
        return run(argv[1], argv[2]);
    } catch (const std::exception& ex) {
        std::cerr << "offline-audio-integration=fail: " << ex.what() << '\n';
        return 1;
    }
}

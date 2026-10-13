// Real-model, no Qt/ALSA: PCM16 WAV -> Silero VAD -> SenseVoice ASR.
// The CI fixture is a recorded wave from a deterministic text-to-speech
// invocation. This is an integration test, NOT a claim about general WER.
#include <audition/backends/sherpa/offline_asr.hpp>
#include <audition/backends/sherpa/vad.hpp>
#include <audition/io/wav_io.hpp>
#include <audition/speech/types.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void verifyMono16k(const audition::AudioBuffer& audio) {
    const auto& format = audio.format();
    if (format.sample_rate_hz != 16000U || format.channel_count != 1U ||
        format.layout != audition::AudioLayout::Interleaved ||
        audio.frameCount() < 16000U)
        throw std::runtime_error("Expected >=1s mono 16kHz WAV");
    for (const auto sample : audio.samples())
        if (!std::isfinite(sample))
            throw std::runtime_error("Non-finite WAV sample");
}
void run(const char* silero, const char* sense, const char* tokens,
         const char* speechPath, const char* outputPath) {
    const auto loaded = audition::io::loadWav(speechPath);
    verifyMono16k(loaded.audio);

    auto options = std::make_unique<audition::SherpaVadOptions>();
    options->sample_rate_hz = 16000U;
    auto& model = std::get<audition::SherpaSileroVadModel>(options->model);
    model.model = silero;
    const audition::SherpaVad detector{std::move(*options)};
    auto session = detector.createSession();
    constexpr std::size_t window = 512U;
    constexpr audition::AudioFormat mono{16000U, 1U, audition::AudioLayout::Interleaved};
    std::size_t activeFrames = 0;
    std::size_t totalFrames = 0;
    const auto& samples = loaded.audio.samples();
    for (std::size_t start = 0; start + window <= samples.size();
         start += window) {
        const audition::AudioView frame{samples.data() + start, window,
                                        mono, audition::Timestamp{}};
        const auto observed = session->process(frame);
        if (observed.speech_active) ++activeFrames;
        ++totalFrames;
    }
    // The model has really been invoked on every 512-sample frame. We do
    // not invent Silero speech-recall thresholds for synthetic TTS.
    session->reset();
    const std::vector<float> silence(window, 0.0F);
    for (int i = 0; i < 12; ++i) {
        const audition::AudioView frame{silence.data(), silence.size(),
                                        mono, audition::Timestamp{}};
        const auto observation = session->process(frame);
        if (observation.speech_active)
            throw std::runtime_error("Silero false-positive on reset silence");
    }

    auto asrOptions = std::make_unique<audition::SherpaOfflineAsrOptions>();
    asrOptions->features.sample_rate_hz = 16000U;
    asrOptions->model = audition::SherpaOfflineSenseVoiceModel{sense, "en", false};
    asrOptions->tokens = tokens;
    audition::SherpaOfflineAsr asr{std::move(*asrOptions)};

    audition::SpeechSegment segment{};
    segment.segment_id = audition::SpeechSegmentId{73U};
    segment.audio = loaded.audio;
    const auto transcript = asr.transcribe(segment);
    if (transcript.segment_id != segment.segment_id)
        throw std::runtime_error("SenseVoice lost segment ID");
    if (transcript.text.empty())
        throw std::runtime_error("SenseVoice produced an empty transcript from speech");

    // The same payload must give the same decoded text on the same CPU/model;
    // this is a strict regression invariant, not a subjective quality score.
    const auto second = asr.transcribe(segment);
    if (second.text != transcript.text || second.segment_id != segment.segment_id)
        throw std::runtime_error("Nondeterministic repeated SenseVoice decode");

    std::ofstream out{outputPath};
    if (!out) throw std::runtime_error("Cannot write speech integration report");
    out << "model=SenseVoiceSmall-INT8\n"
        << "sample_rate_hz=16000\n"
        << "samples=" << samples.size() << '\n'
        << "vad_frames=" << totalFrames << '\n'
        << "vad_active_frames=" << activeFrames << '\n'
        << "asr_text=" << transcript.text << '\n';
    if (!out) throw std::runtime_error("Cannot flush speech report");
    std::cout << "real-speech-integration=ok"
              << " vad_active=" << activeFrames << '/' << totalFrames
              << " asr_chars=" << transcript.text.size() << '\n';
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: real_speech_model_replay SILERO SENSEVOICE TOKENS SPEECH.wav OUT.txt\n";
        return 2;
    }
    try {
        run(argv[1], argv[2], argv[3], argv[4], argv[5]);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "real-speech-integration=fail: " << e.what() << '\n';
        return 1;
    }
}

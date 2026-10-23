#include "alsa_live_capture.hpp"
#include <audition/dsp/source_gain.hpp>
#include "live_audio_tagging_worker.hpp"
#include "source_track_lifecycle.hpp"
#include "speech_epoch_recording_cleanup.hpp"
#include <audition/recording/source_wav_recorder.hpp>
#include <audition/recording/source_recording_session.hpp>
#include <audition/dsp/streaming_level_meter.hpp>
#include <audition/pipeline/source_track_lifecycle.hpp>
#include <audition/audio/recording_timeline_json.hpp>
#include <filesystem>
#include <chrono>
#include <cstdio>
#include "odas_respeaker_profile.hpp"
#if LIBAUDITION_DEMO_HAS_SHERPA
#include <audition/backends/sherpa/vad.hpp>
#include <audition/backends/sherpa/offline_asr.hpp>
#include <audition/speech/per_source_speech_runtime.hpp>
#include <audition/speech/async_speech_ingress_worker.hpp>
#endif

#if LIBAUDITION_DEMO_HAS_ALSA
#include <audition/backends/odas.hpp>
#include <audition/backends/odas/source_elevation_prior.hpp>
#include <alsa/asoundlib.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <memory>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <utility>
#include <vector>

namespace demo {
namespace {

#if LIBAUDITION_DEMO_HAS_ALSA
[[nodiscard]] std::string errorText(const std::string& context, int error) {
    return context + ": " + snd_strerror(error);
}

void requireAlsa(int result, const std::string& operation) {
    if (result < 0) {
        throw std::runtime_error{errorText(operation, result)};
    }
}

struct PcmCloser {
    void operator()(snd_pcm_t* handle) const noexcept {
        if (handle != nullptr) {
            snd_pcm_close(handle);
        }
    }
};

using PcmHandle = std::unique_ptr<snd_pcm_t, PcmCloser>;

struct CtlCloser {
    void operator()(snd_ctl_t* handle) const noexcept {
        if (handle != nullptr) {
            snd_ctl_close(handle);
        }
    }
};

using CtlHandle = std::unique_ptr<snd_ctl_t, CtlCloser>;

bool isReSpeaker(const std::string& name) {
    auto lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("respeaker") != std::string::npos ||
           lower.find("seeed") != std::string::npos ||
           lower.find("4 mic array") != std::string::npos;
}

[[nodiscard]] PcmHandle openPcm(const LiveCaptureConfig& config) {
    if (config.pcm_name.empty()) {
        throw std::runtime_error{"Choose an ALSA capture device"};
    }
    if (config.channel_count == 0U || config.sample_rate_hz == 0U ||
        config.hop_size == 0U) {
        throw std::runtime_error{"Invalid capture format"};
    }

    snd_pcm_t* raw = nullptr;
    // Nonblocking open avoids waiting indefinitely if a USB device is busy.
    requireAlsa(snd_pcm_open(&raw, config.pcm_name.c_str(),
                             SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK),
                "Cannot open ALSA capture device " + config.pcm_name);
    PcmHandle pcm{raw};

    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);
    requireAlsa(snd_pcm_hw_params_any(pcm.get(), params), "ALSA hw_params_any");
    requireAlsa(snd_pcm_hw_params_set_access(
                    pcm.get(), params, SND_PCM_ACCESS_RW_INTERLEAVED),
                "ALSA requires interleaved capture");
    requireAlsa(snd_pcm_hw_params_set_format(
                    pcm.get(), params, SND_PCM_FORMAT_S16_LE),
                "ALSA requires signed 16-bit little-endian PCM");
    requireAlsa(snd_pcm_hw_params_set_channels(
                    pcm.get(), params, config.channel_count),
                "ALSA channel count (check -c 6 and device capabilities)");
    requireAlsa(snd_pcm_hw_params_set_rate(
                    pcm.get(), params, config.sample_rate_hz, 0),
                "ALSA sample rate (no implicit resampling)");
    snd_pcm_uframes_t period =
        static_cast<snd_pcm_uframes_t>(config.hop_size);
    int direction = 0;
    requireAlsa(snd_pcm_hw_params_set_period_size_near(
                    pcm.get(), params, &period, &direction),
                "ALSA period size");
    requireAlsa(snd_pcm_hw_params(pcm.get(), params), "ALSA hw_params");
    unsigned int negotiatedRate = 0;
    requireAlsa(snd_pcm_hw_params_get_rate(
                    params, &negotiatedRate, &direction), "ALSA read actual rate");
    if (negotiatedRate != config.sample_rate_hz) {
        throw std::runtime_error{
            "ALSA selected a different sample rate: explicit resampling required"};
    }
    requireAlsa(snd_pcm_prepare(pcm.get()), "ALSA prepare");
    requireAlsa(snd_pcm_nonblock(pcm.get(), 1), "ALSA nonblocking mode");
    return pcm;
}

#endif

}  // namespace

AlsaLiveCapture::~AlsaLiveCapture() {
    stop();
}

std::vector<AlsaCaptureDevice> AlsaLiveCapture::discover() {
    std::vector<AlsaCaptureDevice> devices;
#if LIBAUDITION_DEMO_HAS_ALSA
    int card = -1;
    if (snd_card_next(&card) < 0) {
        return devices;
    }
    while (card >= 0) {
        const auto deviceName = std::string{"hw:"} + std::to_string(card);
        snd_ctl_t* rawCtl = nullptr;
        if (snd_ctl_open(&rawCtl, deviceName.c_str(), 0) >= 0) {
            CtlHandle ctl{rawCtl};
            char* name = nullptr;
            const int nameResult = snd_card_get_name(card, &name);
            std::string cardName = nameResult >= 0 && name != nullptr
                                       ? std::string{name} : deviceName;
            if (name != nullptr) {
                std::free(name);
            }

            int device = -1;
            while (snd_ctl_pcm_next_device(ctl.get(), &device) >= 0 &&
                   device >= 0) {
                snd_pcm_info_t* info = nullptr;
                snd_pcm_info_alloca(&info);
                snd_pcm_info_set_device(info,
                                         static_cast<unsigned int>(device));
                snd_pcm_info_set_subdevice(info, 0U);
                snd_pcm_info_set_stream(info, SND_PCM_STREAM_CAPTURE);
                if (snd_ctl_pcm_info(ctl.get(), info) < 0) {
                    continue; // Playback-only hardware PCM.
                }
                const char* pcmName = snd_pcm_info_get_name(info);
                std::string description = cardName;
                if (pcmName != nullptr) {
                    description += " — ";
                    description += pcmName;
                }
                devices.push_back({
                    deviceName + "," + std::to_string(device),
                    description,
                    isReSpeaker(description)});
            }
        }
        if (snd_card_next(&card) < 0) {
            break;
        }
    }
    std::stable_sort(devices.begin(), devices.end(),
                     [](const auto& a, const auto& b) {
                         return a.likely_respeaker && !b.likely_respeaker;
                     });
#endif
    return devices;
}

void AlsaLiveCapture::start(LiveCaptureConfig config) {
    stop();
    if (config.pcm_name.empty()) {
        throw std::runtime_error{"Select an ALSA input (or enter hw:N,M)"};
    }
    {
        std::lock_guard<std::mutex> lock{mutex_};
        latest_ = {};
        latest_.running = true;
        speech_gain_enabled_ = config.speech_agc_enabled;
        speech_gain_implementation_ = config.speech_agc_implementation;
        speech_gain_revision_ = 0;
        requested_sst_.revision = 0;
        requested_sst_.onset = config.sst_onset;
        requested_sst_.release = config.sst_release;
        requested_sst_.timeout_hops = config.sst_inactive_hops;
    }
    stop_requested_.store(false);
    try {
        thread_ = std::thread{[this, config = std::move(config)]() mutable {
            run(std::move(config));
        }};
    } catch (...) {
        std::lock_guard<std::mutex> lock{mutex_};
        latest_.running = false;
        throw;
    }
}

void AlsaLiveCapture::setSpeechGain(bool enabled, std::string implementation) {
    // Validate before publishing state to the capture thread.
    if (enabled)
        (void)audition::dsp::makeSourceGain(implementation.c_str());
    std::lock_guard<std::mutex> lock{mutex_};
    speech_gain_enabled_ = enabled;
    speech_gain_implementation_ = std::move(implementation);
    ++speech_gain_revision_;
}

void AlsaLiveCapture::setSstTuning(
    double onset, double release, std::uint32_t timeout_hops) {
    if (!std::isfinite(onset) || !std::isfinite(release) ||
        onset <= 0.0 || onset >= 1.0 || release <= 0.0 ||
        release >= onset || timeout_hops == 0U)
        throw std::invalid_argument("Invalid live SST tuning");
    std::lock_guard<std::mutex> lock{mutex_};
    requested_sst_.onset = onset;
    requested_sst_.release = release;
    requested_sst_.timeout_hops = timeout_hops;
    ++requested_sst_.revision;
}

void AlsaLiveCapture::stop() {
    stop_requested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock{mutex_};
    latest_.running = false;
}

LiveCaptureSnapshot AlsaLiveCapture::snapshot() const {
    std::lock_guard<std::mutex> lock{mutex_};
    return latest_;
}

void AlsaLiveCapture::run(LiveCaptureConfig config) noexcept {
#if LIBAUDITION_DEMO_HAS_ALSA
    std::string failureStage{"initialization"};
    try {
        audition::OdasOptions options{};
        options.sample_rate_hz = config.sample_rate_hz;
        options.hop_size = config.hop_size;
        options.frame_size = config.frame_size;
        options.input_channels = config.input_channels;
        for (const auto& xyz : config.microphone_positions) {
            options.microphone_array.microphones.push_back(
                audition::MicrophoneGeometry{{xyz[0], xyz[1], xyz[2]}});
        }
        if (config.respeaker_angular_profile) {
            audition::applyReSpeakerUsb4MicAngularProfile(options);
        }
        if (config.near_plane_sources) {
            audition::applyOdasNearPlaneSourcePrior(options);
        }
        // Tune ODAS SST itself. No post-hoc filtering, source gating, or
        // ODAS reset/reconstruction on acoustic silence.
        if (config.tune_sst) {
            if (config.sst_onset <= 0.0 || config.sst_onset >= 1.0 ||
                config.sst_release <= 0.0 ||
                config.sst_release >= config.sst_onset ||
                config.sst_inactive_hops == 0U)
                throw std::invalid_argument("Invalid ODAS SST thresholds");
            if (options.sst.active_gmm.empty() ||
                options.sst.inactive_gmm.empty())
                throw std::runtime_error("ODAS SST GMM parameters unavailable");
            options.sst.active_gmm.front().mean = config.sst_onset;
            options.sst.inactive_gmm.front().mean = config.sst_release;
            options.sst.inactive_timeout_hops = config.sst_inactive_hops;
        }
        // Source-specific classification needs actual SSS mono audio. Avoid
        // the extra DSP cost when no classifier model is configured.
        options.sss.enabled = true; // source WAV recording is available independently
        audition::OdasSpatialEngine engine{options};
        std::uint64_t appliedSstRevision = 0;
        std::string lastSstTuningError;
        std::unique_ptr<LiveAudioTaggingWorker> classifier;
        if (config.classify_sources) {
            classifier = std::make_unique<LiveAudioTaggingWorker>(
                LiveTaggingOptions{config.tagging_model_path,
                                   config.tagging_labels_path,
                                   config.tagging_ced_model,
                                   config.tagging_yamnet_model,
                                   config.sample_rate_hz,
                                   config.hop_size,
                                   audition::VocabularySelection{
                                       config.tagging_selected_vocabulary
                                           ? audition::VocabularyMode::SelectedClasses
                                           : audition::VocabularyMode::AllClasses,
                                       config.tagging_vocabulary_labels,
                                       5U},
                                   config.tagging_efficientat_model});
        }
#if LIBAUDITION_DEMO_HAS_SHERPA
        std::unique_ptr<audition::SherpaVad> speech_vad;
        std::unique_ptr<audition::PerSourceSpeechRuntime> speech_runtime;
        std::unique_ptr<audition::AsyncSpeechIngressWorker> speech_worker;
        std::map<std::uint64_t, std::string> transcripts;
        std::map<std::uint64_t, std::unique_ptr<audition::dsp::ISourceGain>> speechGains;
        std::uint64_t appliedGainRevision = 0;
        if (config.transcribe_sources) {
            audition::SherpaVadOptions vad_options{};
            vad_options.sample_rate_hz = config.sample_rate_hz;
            vad_options.model = audition::SherpaSileroVadModel{
                config.speech_vad_model_path};
            speech_vad = std::make_unique<audition::SherpaVad>(std::move(vad_options));
            // Keep the options on the heap: older GCC -O3 builds can
            // spuriously report an uninitialized moved-from std::map when a
            // large default-constructed options aggregate lives on the stack.
            auto asr_options = std::make_unique<audition::SherpaOfflineAsrOptions>();
            asr_options->features.sample_rate_hz = config.sample_rate_hz;
            asr_options->tokens = config.speech_asr_tokens_path;
            asr_options->model = audition::SherpaOfflineSenseVoiceModel{
                config.speech_asr_model_path};
            auto asr = std::make_shared<audition::SherpaOfflineAsr>(std::move(*asr_options));
            audition::PerSourceSpeechOptions pipeline_options{};
            pipeline_options.sample_rate_hz = config.sample_rate_hz;
            speech_runtime = std::make_unique<audition::PerSourceSpeechRuntime>(
                *speech_vad, std::move(asr), pipeline_options);
            audition::SpeechPcmIngressOptions ingress_options{};
            ingress_options.sample_rate_hz = config.sample_rate_hz;
            ingress_options.max_chunk_samples =
                std::min<std::size_t>(config.hop_size, config.sample_rate_hz);
            speech_worker = std::make_unique<audition::AsyncSpeechIngressWorker>(
                *speech_runtime, ingress_options);
        }
#else
        if (config.transcribe_sources) {
            throw std::runtime_error{"Per-source ASR requires the sherpa-onnx backend"};
        }
#endif
        audition::pipeline::SourceTrackLifecycle source_lifecycle;
        const auto session_stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        audition::recording::SourceWavRecorder recorder{
            std::filesystem::temp_directory_path() / "libaudition-recordings" /
                ("session-" + std::to_string(session_stamp)), config.sample_rate_hz};
        audition::recording::SourceRecordingSession recordings;
        // Track only active ODAS generations denied a timeline. Retrying
        // midway through a WAV would shift its annotation clock origin.
        std::map<std::uint64_t, std::uint64_t> skipped_annotation_recordings;
        std::uint64_t annotation_recordings_skipped = 0;
        std::uint64_t recoverableErrors = 0;
        std::string lastRecoverableError;
        const auto reportRecoverable = [&](const char* phase, const std::exception& e) {
            ++recoverableErrors;
            lastRecoverableError = std::string{phase} + ": " + e.what();
            if (recoverableErrors <= 3U || recoverableErrors % 100U == 0U) {
                std::fprintf(stderr,
                    "[libaudition] live capture recovered (%s, count=%llu): %s\n",
                    phase, static_cast<unsigned long long>(recoverableErrors),
                    e.what());
                std::fflush(stderr);
            }
        };
#if LIBAUDITION_DEMO_HAS_SHERPA
        using SpeechEpochKey = std::pair<std::uint64_t, std::uint64_t>;
        std::map<SpeechEpochKey, audition::RecordingId> epoch_recordings;
        // Worker Forget can cancel an unconsumed FIFO Retire. Keep the
        // affected deferred WAV identities so they can be finalized.
        std::set<audition::RecordingId> deferred_ack_recordings;
        std::uint64_t retirement_capacity_drops = 0;
        std::uint64_t speech_worker_errors = 0;
        std::string last_speech_worker_error;
        const auto drainSpeechAcks = [&]() {
            if (!speech_worker) return;
            for (const auto& event : speech_worker->drainAcknowledgements()) {
                try {
                const SpeechEpochKey key{event.source_id,event.epoch};
                const auto mapped = epoch_recordings.find(key);
                switch (event.kind) {
                case audition::AsyncSpeechIngressWorker::Acknowledgement::Kind::Bound:
                    if (mapped!=epoch_recordings.end() && event.generation)
                        recordings.bindRecordingAsr(mapped->second,*event.generation);
                    break;
                case audition::AsyncSpeechIngressWorker::Acknowledgement::Kind::Retired:
                    if (mapped!=epoch_recordings.end()) {
                        (void)recordings.tryAcknowledgeRetired(
                            mapped->second,event.generation);
                        deferred_ack_recordings.erase(mapped->second);
                        epoch_recordings.erase(mapped);
                    }
                    if (!event.generation) ++retirement_capacity_drops;
                    break;
                case audition::AsyncSpeechIngressWorker::Acknowledgement::Kind::Forgot:
                    if (event.generation)
                        recordings.cancelGeneration(*event.generation);
                    // Only epochs older than this invalidation may be removed.
                    // A new Bound may already have been applied for this ID.
                    clearForgottenSpeechRecordingEpochs(
                        epoch_recordings,deferred_ack_recordings,recordings,
                        event.source_id,event.epoch);
                    break;
                case audition::AsyncSpeechIngressWorker::Acknowledgement::Kind::Error:
                    if (event.generation)
                        recordings.cancelGeneration(*event.generation);
                    ++speech_worker_errors;
                    last_speech_worker_error=event.error.substr(0,200);
                    break;
                }
                } catch (const std::exception& e) {
                    reportRecoverable("speech acknowledgement", e);
                }
            }
        };
#endif
        const auto drainClassification = [&]() {
            if (!classifier) return;
            for (const auto& [sourceId, event] : classifier->takeCompleted()) {
                recordings.addSound(sourceId,
                    event.window_start_sample, event.window_end_sample,
                    event.label,
                    config.tagging_yamnet_model ? "YAMNet" :
                    config.tagging_efficientat_model ? "EfficientAT" : "Sherpa",
                    event.probability, event.stage);
            }
        };
#if LIBAUDITION_DEMO_HAS_SHERPA
        const auto drainAsr = [&]() {
            if (!speech_runtime) return;
            try {
            // The capture producer owns recordings. Apply worker generation
            // acknowledgements before consuming ASR results or retiring JSON.
            drainSpeechAcks();
            auto batch = speech_runtime->drainReady();
            for (auto& result : batch.results) {
                recordings.addSpeech(result);
                const auto active = speech_runtime->activeGeneration(result.track_id);
                if (active && *active == result.generation &&
                    recordings.active(result.track_id)) {
                    transcripts[result.track_id] = std::move(result.transcript.text);
                }
            }
            for (const auto generation : batch.ready_retired_generations) {
                recordings.completeGeneration(generation);
                speech_runtime->releaseRetired(generation);
            }
            } catch (const std::exception& e) {
                reportRecoverable("ASR completion", e);
            }
        };
#endif
        const auto retireSource = [&](std::uint64_t trackId) {
            recorder.retire(trackId); // always close WAV before best-effort metadata handling
            try {
            skipped_annotation_recordings.erase(trackId);
            if (classifier) classifier->forget(trackId);
#if LIBAUDITION_DEMO_HAS_SHERPA
            speechGains.erase(trackId);
#endif
#if LIBAUDITION_DEMO_HAS_SHERPA
            if (speech_worker) {
                // FIFO Retire remains behind accepted audio. The ASR
                // generation is acknowledged later, by WAV recording ID.
                const auto epoch=speech_worker->retire(trackId);
                if (epoch) {
                    const auto recording_id=recordings.retireDeferred(trackId);
                    if (recording_id) {
                        epoch_recordings[{trackId,*epoch}]=*recording_id;
                        deferred_ack_recordings.insert(*recording_id);
                    }
                } else {
                    // No PCM was admitted to the worker for this lifecycle.
                    recordings.retire(trackId,std::nullopt);
                }
                transcripts.erase(trackId);
            } else {
                recordings.retire(trackId,std::nullopt);
            }
#else
            recordings.retire(trackId, std::nullopt);
#endif
            } catch (const std::exception& e) {
                reportRecoverable("retire source", e);
            }
        };
        auto pcm = openPcm(config);

        const auto channelCount = static_cast<std::size_t>(config.channel_count);
        const auto hop = static_cast<std::size_t>(config.hop_size);
        std::vector<std::int16_t> native(hop * channelCount, 0);
        std::uint64_t hops = 0;
        std::uint64_t xruns = 0;
        std::size_t filled = 0;
        std::vector<audition::dsp::StreamingLevelMeter> channelMeters(channelCount);
        std::map<std::uint64_t, audition::dsp::StreamingLevelMeter> sssMeters;

        const std::uint64_t visualStride = std::max<std::uint64_t>(
            1U, static_cast<std::uint64_t>(
                std::round(static_cast<double>(config.sample_rate_hz) /
                           (20.0 * static_cast<double>(config.hop_size)))));
        while (!stop_requested_.load()) {
#if LIBAUDITION_DEMO_HAS_SHERPA
            {
                bool enabled = false;
                std::string implementation;
                std::uint64_t revision = 0;
                {
                    std::lock_guard<std::mutex> lock{mutex_};
                    enabled = speech_gain_enabled_;
                    implementation = speech_gain_implementation_;
                    revision = speech_gain_revision_;
                }
                if (revision != appliedGainRevision) {
                    if (enabled) {
                        // Create the replacement before modifying active state.
                        (void)audition::dsp::makeSourceGain(implementation.c_str());
                    }
                    speechGains.clear();
                    config.speech_agc_enabled = enabled;
                    config.speech_agc_implementation = std::move(implementation);
                    appliedGainRevision = revision;
                }
            }
#endif
            if (config.tune_sst) {
                SstTuningRequest requested;
                {
                    std::lock_guard<std::mutex> lock{mutex_};
                    requested = requested_sst_;
                }
                if (requested.revision != appliedSstRevision) {
                    // Apply on the capture owner thread between ODAS hops.
                    // Preserve tracker state, source identities and WAV/ASR epochs.
                    try {
                        engine.setSstTuning(requested.onset, requested.release,
                                            requested.timeout_hops);
                        appliedSstRevision = requested.revision;
                        lastSstTuningError.clear();
                    } catch (const std::exception& e) {
                        lastSstTuningError = e.what();
                        std::fprintf(stderr, "[libaudition] SST tuning rejected (prior values retained): %s\n",
                                     e.what());
                        appliedSstRevision = requested.revision;
                    }
                }
            }
            // readi() can return a short interleaved frame count.
            const auto result = snd_pcm_readi(
                pcm.get(), native.data() + filled * channelCount,
                static_cast<snd_pcm_uframes_t>(hop - filled));
            if (result == -EAGAIN) {
                const int waited = snd_pcm_wait(pcm.get(), 100);
                if (waited < 0 && waited != -EINTR) {
                    throw std::runtime_error{errorText("ALSA wait", waited)};
                }
                continue;
            }
            if (result == -EINTR) {
                continue;
            }
            if (result == -EPIPE || result == -ESTRPIPE) {
                ++xruns;
                filled = 0;
                for (auto& meter : channelMeters) meter.reset();
                sssMeters.clear();
                requireAlsa(snd_pcm_prepare(pcm.get()), "ALSA overrun recovery");
                failureStage = "ODAS reset after ALSA xrun";
                engine.reset(); // discontinuous samples must not share ODAS state
                drainClassification();
                source_lifecycle.clear([&](std::uint64_t id) {
                    retireSource(id);
                });
#if LIBAUDITION_DEMO_HAS_SHERPA
                drainAsr();
#endif
                continue;
            }
            if (result < 0) {
                throw std::runtime_error{errorText("ALSA capture", static_cast<int>(result))};
            }
            if (result == 0) {
                // Do not spin if a plugin reports no samples.
                (void)snd_pcm_wait(pcm.get(), 50);
                continue;
            }
            filled += static_cast<std::size_t>(result);
            if (filled < hop) {
                continue;
            }
            filled = 0;
            std::vector<float> samples;
            samples.reserve(native.size());
            for (std::size_t i = 0; i < native.size(); ++i) {
                const float sample = static_cast<float>(native[i]) / 32768.0F;
                samples.push_back(sample);
                const auto channel = i % channelCount;
                channelMeters[channel].push(sample);
            }
            const auto timestamp = audition::Timestamp{
                static_cast<std::int64_t>(
                    (static_cast<double>(hops) *
                     static_cast<double>(config.hop_size) * 1.e9) /
                    static_cast<double>(config.sample_rate_hz)),
                {audition::ClockDomain::Monotonic, 2U}};
            audition::AudioBuffer block{
                std::move(samples),
                {config.sample_rate_hz, config.channel_count,
                 audition::AudioLayout::Interleaved},
                timestamp, hops};
            failureStage = "ODAS process";
            const auto resultFrame = engine.process(block.view());
            ++hops;
            { // Recording is independent of classifier and ASR toggles.
                std::set<std::uint64_t> present;
                for (const auto& track : resultFrame.tracks) {
                    present.insert(track.track_id.value());
                }
                for (const auto& separated : resultFrame.separated_frames) {
                    present.insert(separated.track_id.value());
                }
                drainClassification();
                source_lifecycle.update(std::move(present), [&](std::uint64_t id) {
                    retireSource(id);
                });
                for (const auto& separated : resultFrame.separated_frames) {
                    sssMeters[separated.track_id.value()].push(
                        separated.audio.samples().data(),
                        separated.audio.samples().size());
                    const auto sourceId = separated.track_id.value();
                    const auto recorded = recorder.append(sourceId, separated.audio.samples());
                    if (recorded.recording_id != 0) {
                        if (!recordings.active(sourceId) &&
                            skipped_annotation_recordings.count(sourceId) == 0) {
                            if (auto entry = recorder.activeEntry(sourceId)) {
                                // Keep WAV, audio capture and inference
                                // running when annotation memory is full.
                                if (!recordings.tryBegin(sourceId, entry->recording_id,
                                        entry->path, config.sample_rate_hz,
                                        separated.audio.captureTime())) {
                                    skipped_annotation_recordings.emplace(
                                        sourceId, entry->recording_id);
                                    ++annotation_recordings_skipped;
                                }
                            }
                        }
                        if (recorded.lost_samples != 0)
                            recordings.addGap(sourceId, recorded.start_sample +
                                recorded.accepted_samples, recorded.lost_samples);
                    }
                    if (classifier) {
                        classifier->push(separated.track_id.value(),
                                         separated.audio.samples(), hops);
                    }
#if LIBAUDITION_DEMO_HAS_SHERPA
                    if (speech_worker) {
                        // Associate the WAV with the exact admitted PCM epoch.
                        // A separate sourceEpoch() lookup can see a reused ID.
                        std::vector<float> adjusted;
                        auto speechView = separated.audio.view();
                        if (config.speech_agc_enabled) {
                            auto& processor = speechGains[sourceId];
                            if (!processor)
                                processor = audition::dsp::makeSourceGain(
                                    config.speech_agc_implementation.c_str());
                            adjusted = processor->process(
                                separated.audio.samples().data(),
                                separated.audio.samples().size(), config.sample_rate_hz);
                            speechView = audition::AudioView{
                                adjusted.data(), adjusted.size(), speechView.format(),
                                speechView.captureTime(), speechView.sequenceNumber()};
                        }
                        const auto admission=speech_worker->pushWithEpoch(
                            sourceId,speechView);
                        if (admission.admission==audition::SpeechPcmAdmission::Accepted) {
                            const auto recording_id=recordings.recordingId(sourceId);
                            if (admission.epoch && recording_id)
                                epoch_recordings[{sourceId,*admission.epoch}]=*recording_id;
                        }
                    }
#endif
                }
                drainClassification();
#if LIBAUDITION_DEMO_HAS_SHERPA
                drainAsr();
#endif
            }
            if (hops % visualStride != 0U) {
                continue;
            }
            std::vector<AcousticSceneTrack> tracks;
            tracks.reserve(resultFrame.tracks.size());
            for (const auto& t : resultFrame.tracks) {
                const auto xyz = t.direction.direction.vector();
                AcousticSceneTrack track{
                    t.track_id.value(),
                    QVector3D{static_cast<float>(xyz.x),
                              static_cast<float>(xyz.y),
                              static_cast<float>(xyz.z)},
                    t.activity.value()};
#if LIBAUDITION_DEMO_HAS_SHERPA
                if (const auto spoken = transcripts.find(t.track_id.value());
                    spoken != transcripts.end()) {
                    track.transcript = spoken->second;
                }
#endif
                if (classifier) {
                    const auto tag = classifier->result(t.track_id.value(), hops);
                    if (tag.has_value()) {
                        track.classification_label = tag->top_classes.empty()
                            ? "No selected classes above the displayed results"
                            : tag->top_classes;
                        if (!tag->unsupported_labels.empty()) {
                            track.classification_label += " | Unsupported: ";
                            for (std::size_t j = 0; j < tag->unsupported_labels.size(); ++j) {
                                if (j != 0U) track.classification_label += ", ";
                                track.classification_label += tag->unsupported_labels[j];
                            }
                        }
                        if (!tag->stage.empty()) {
                            const auto phase = tag->stage == "quick"
                                ? "Fast (~1s)" : "Refined (5s)";
                            track.classification_label = phase + std::string{": "} +
                                                         track.classification_label;
                        }
                        track.classification_probability = tag->probability;

                    }
                }
                tracks.push_back(std::move(track));
            }
            std::vector<AcousticScenePotential> proposals;
            proposals.reserve(resultFrame.potential_sources.size());
            for (const auto& p : resultFrame.potential_sources) {
                const auto d = p.direction.vector();
                proposals.push_back({
                    QVector3D{static_cast<float>(d.x),
                              static_cast<float>(d.y),
                              static_cast<float>(d.z)}, p.score});
            }
            std::vector<double> rmsDbfs;
            std::vector<double> peakDbfs;
            rmsDbfs.reserve(channelCount);
            peakDbfs.reserve(channelCount);
            for (auto& meter : channelMeters) {
                const auto snapshot = meter.snapshotAndReset();
                rmsDbfs.push_back(snapshot.rms_dbfs);
                peakDbfs.push_back(snapshot.peak_dbfs);
            }
            std::vector<LiveCaptureSnapshot::SourceLevel> sssLevels;
            sssLevels.reserve(sssMeters.size());
            for (auto& [trackId, meter] : sssMeters) {
                const auto level = meter.snapshotAndReset();
                if (level.sample_count == 0) continue;
                sssLevels.push_back({trackId,
                    level.rms_dbfs, level.peak_dbfs, level.sample_count});
            }
            sssMeters.clear();
            std::lock_guard<std::mutex> lock{mutex_};
            latest_.tracks = std::move(tracks);
            latest_.recordings.clear();
            for (const auto& item : recorder.entries()) {
                latest_.recordings.push_back({item.recording_id, item.track_id, item.path,
                                               item.samples, item.active, item.lost_samples, item.error});
            }
            latest_.potentials = std::move(proposals);
            latest_.channel_rms_dbfs = std::move(rmsDbfs);
            latest_.channel_peak_dbfs = std::move(peakDbfs);
            latest_.sss_levels = std::move(sssLevels);
            latest_.speech_gain_levels.clear();
#if LIBAUDITION_DEMO_HAS_SHERPA
            for (const auto& [id, processor] : speechGains)
                if (processor)
                    latest_.speech_gain_levels.push_back({id, processor->gainDb()});
#endif
            latest_.processed_hops = hops;
            latest_.recoveries = xruns;
            latest_.classification_error = classifier ? classifier->error() : "";
            if (!lastSstTuningError.empty()) {
                if (!latest_.classification_error.empty()) latest_.classification_error += " | ";
                latest_.classification_error += "SST tuning rejected: " + lastSstTuningError;
            }
            latest_.classifier_status = classifier && !resultFrame.tracks.empty()
                ? classifier->status(resultFrame.tracks.front().track_id.value())
                : (classifier ? "Waiting for ODAS tracks" : "Disabled");
#if LIBAUDITION_DEMO_HAS_SHERPA
            latest_.speech_status = "Disabled";
            if (speech_runtime) {
                const auto stats = speech_runtime->statistics();
                latest_.speech_status = "ASR completed=" +
                    std::to_string(stats.completed_segments) +
                    " queued=" + std::to_string(stats.pending_segments) +
                    " dropped=" + std::to_string(stats.dropped_segments) +
                    " retired=" + std::to_string(stats.retired_generations) +
                    " retire_capacity_drops=" +
                    std::to_string(retirement_capacity_drops) +
                    " worker_errors=" + std::to_string(speech_worker_errors);
                if (speech_worker) {
                    const auto input=speech_worker->inputStatistics();
                    latest_.speech_status +=
                        " PCM queued=" + std::to_string(input.pending_chunks) +
                        " PCM dropped=" + std::to_string(input.rejected_chunks) +
                        " PCM invalidated=" + std::to_string(input.invalidated_chunks) +
                        " retire_wait=" + std::to_string(input.pending_retirements);
                }
                if (!last_speech_worker_error.empty())
                    latest_.speech_status +=
                        " | Speech worker: " + last_speech_worker_error;
                if (!resultFrame.tracks.empty()) {
                    const auto id = resultFrame.tracks.front().track_id.value();
                    const auto source = speech_runtime->trackStatus(id);
                    if (source.error) {
                        latest_.speech_status += " | Track " + std::to_string(id) +
                            " VAD error: " + *source.error;
                    } else if (source.exists) {
                        latest_.speech_status += source.speech_active
                            ? " | VAD: speech" : " | VAD: silence";
                        latest_.speech_status += " buffered=" +
                            std::to_string(source.buffered_samples);
                    }
                }
                if (stats.last_error)
                    latest_.speech_status += " | Error: " + *stats.last_error;
            }
#else
            latest_.speech_status = "Requires Sherpa";
#endif
            const auto annotation_drops = recordings.droppedAnnotations();
            latest_.speech_status += " | Sidecars skipped=" +
                std::to_string(annotation_recordings_skipped);
            latest_.speech_status += " | Annotations dropped: speech=" +
                std::to_string(annotation_drops.speech) +
                " sound=" + std::to_string(annotation_drops.sound) +
                " gaps=" + std::to_string(annotation_drops.gaps);
            latest_.speech_status += " | WAV dropped=" + std::to_string(recorder.dropped()) +
                " | WAV admissions denied=" + std::to_string(recorder.admissionDrops()) +
                " | WAV outstanding=" + std::to_string(recorder.outstandingRecordings()) +
                " | WAV history pruned=" + std::to_string(recorder.historyEvicted());
            if (recordings.persistenceError())
                latest_.speech_status += " | JSON error: " + *recordings.persistenceError();
            if (recoverableErrors != 0U) {
                latest_.speech_status += " | recoverable errors=" +
                    std::to_string(recoverableErrors) + " | last: " + lastRecoverableError;
            }
            for (const auto& item : recorder.entries()) {
                if (!item.error.empty()) {
                    latest_.speech_status += " | WAV error: " + item.error;
                    break;
                }
            }
            ++latest_.generation;
        }
        drainClassification();
        source_lifecycle.clear([&](std::uint64_t id) { retireSource(id); });
#if LIBAUDITION_DEMO_HAS_SHERPA
        if (speech_runtime) {
            // Stop accepting PCM, then drain FIFO VAD/retirement controls
            // and async ASR completions on the recording-owner thread.
            if (speech_worker) speech_worker->closeInput();
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds{10};
            while (((speech_worker && !speech_worker->finished()) ||
                    recordings.pendingRetirements()!=0) &&
                   std::chrono::steady_clock::now() < deadline) {
                drainAsr();
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
            }
            drainAsr();
        }
#endif
        recordings.flushAll();
    } catch (const std::exception& error) {
        const auto message = failureStage + ": " + error.what();
        std::fprintf(stderr, "[libaudition] live capture terminated: %s\n",
                     message.c_str());
        std::fflush(stderr);
        std::lock_guard<std::mutex> lock{mutex_};
        if (!stop_requested_.load()) latest_.error = message;
    } catch (...) {
        std::fprintf(stderr, "[libaudition] live capture terminated: unknown error (%s)\n",
                     failureStage.c_str());
        std::fflush(stderr);
        std::lock_guard<std::mutex> lock{mutex_};
        if (!stop_requested_.load())
            latest_.error = failureStage + ": unknown exception";
    }
#else
    static_cast<void>(config);
    {
        std::lock_guard<std::mutex> lock{mutex_};
        latest_.error = "ALSA live capture requires Linux, libasound and ODAS";
    }
#endif
    {
        std::lock_guard<std::mutex> lock{mutex_};
        latest_.running = false;
    }
}

}  // namespace demo

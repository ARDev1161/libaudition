#include "alsa_live_capture.hpp"
#include "live_audio_tagging_worker.hpp"
#include "odas_respeaker_profile.hpp"

#if LIBAUDITION_DEMO_HAS_ALSA
#include <audition/backends/odas.hpp>
#include <alsa/asoundlib.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <memory>
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
            applyReSpeakerUsb4MicAngularProfile(options);
        }
        // Source-specific classification needs actual SSS mono audio. Avoid
        // the extra DSP cost when no classifier model is configured.
        options.sss.enabled = config.classify_sources;
        audition::OdasSpatialEngine engine{options};
        std::unique_ptr<LiveAudioTaggingWorker> classifier;
        if (config.classify_sources) {
            classifier = std::make_unique<LiveAudioTaggingWorker>(
                LiveTaggingOptions{config.tagging_model_path,
                                   config.tagging_labels_path,
                                   config.tagging_ced_model,
                                   config.tagging_yamnet_model,
                                   config.sample_rate_hz,
                                   config.hop_size});
        }
        auto pcm = openPcm(config);

        const auto channelCount = static_cast<std::size_t>(config.channel_count);
        const auto hop = static_cast<std::size_t>(config.hop_size);
        std::vector<std::int16_t> native(hop * channelCount, 0);
        std::uint64_t hops = 0;
        std::uint64_t xruns = 0;
        std::size_t filled = 0;
        // Aggregate per-channel PCM levels between UI snapshots (~50 ms).
        // Raw hardware channels include the USB reference channels as well
        // as the four actual microphone channels.
        std::vector<double> sumSquares(channelCount, 0.0);
        std::vector<double> maxAbs(channelCount, 0.0);
        std::size_t measuredFrames = 0;
        const std::uint64_t visualStride = std::max<std::uint64_t>(
            1U, static_cast<std::uint64_t>(
                std::round(static_cast<double>(config.sample_rate_hz) /
                           (20.0 * static_cast<double>(config.hop_size)))));
        while (!stop_requested_.load()) {
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
                std::fill(sumSquares.begin(), sumSquares.end(), 0.0);
                std::fill(maxAbs.begin(), maxAbs.end(), 0.0);
                measuredFrames = 0;
                requireAlsa(snd_pcm_prepare(pcm.get()), "ALSA overrun recovery");
                engine.reset(); // discontinuous samples must not share ODAS state
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
                const double level = static_cast<double>(sample);
                sumSquares[channel] += level * level;
                maxAbs[channel] = std::max(maxAbs[channel], std::abs(level));
            }
            measuredFrames += hop;
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
            const auto resultFrame = engine.process(block.view());
            ++hops;
            if (classifier) {
                for (const auto& separated : resultFrame.separated_frames) {
                    classifier->push(separated.track_id.value(),
                                     separated.audio.samples(), hops);
                }
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
                if (classifier) {
                    const auto tag = classifier->result(t.track_id.value(), hops);
                    if (tag.has_value()) {
                        track.classification_label = tag->label;
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
            for (std::size_t ch = 0; ch < channelCount; ++ch) {
                const double rms = std::sqrt(
                    sumSquares[ch] / static_cast<double>(measuredFrames));
                rmsDbfs.push_back(20.0 * std::log10(std::max(rms, 1.0e-9)));
                peakDbfs.push_back(20.0 * std::log10(std::max(maxAbs[ch], 1.0e-9)));
            }
            std::fill(sumSquares.begin(), sumSquares.end(), 0.0);
            std::fill(maxAbs.begin(), maxAbs.end(), 0.0);
            measuredFrames = 0;
            std::lock_guard<std::mutex> lock{mutex_};
            latest_.tracks = std::move(tracks);
            latest_.potentials = std::move(proposals);
            latest_.channel_rms_dbfs = std::move(rmsDbfs);
            latest_.channel_peak_dbfs = std::move(peakDbfs);
            latest_.processed_hops = hops;
            latest_.recoveries = xruns;
            latest_.classification_error = classifier ? classifier->error() : "";
            ++latest_.generation;
        }
    } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lock{mutex_};
        if (!stop_requested_.load()) {
            latest_.error = error.what();
        }
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

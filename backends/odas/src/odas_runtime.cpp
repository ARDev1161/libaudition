#include "detail/odas_runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include <audition/core/error.hpp>

#include "detail/odas_config.hpp"
#include "detail/odas_options_detail.hpp"

namespace audition::odas_detail {
namespace {

template <typename T, void (*Destroy)(T*)>
struct OdasDeleter {
    void operator()(T* value) const noexcept {
        if (value != nullptr) {
            Destroy(value);
        }
    }
};

template <typename T, void (*Destroy)(T*)>
using OdasUniquePtr = std::unique_ptr<T, OdasDeleter<T, Destroy>>;

template <typename T>
T* requireAllocated(T* value) {
    if (value == nullptr) {
        throw std::bad_alloc{};
    }
    return value;
}

void processOrThrow(int status, const char* message) {
    if (status != 0) {
        throw Error{ErrorCode::ProcessingError, message};
    }
}

}  // namespace

class OdasRuntime::Impl {
public:
    explicit Impl(const OdasOptions& options)
        : options_(options), input_channels_(resolvedInputChannels(options)) {
        rebuild();
    }

    ~Impl() { destroyRuntime(); }

    void reset() {
        destroyRuntime();
        odas_timestamp_ = 0U;
        rebuild();
    }

    [[nodiscard]] OdasRuntimeResult process(AudioView audio) {
        validateInput(audio);
        ++odas_timestamp_;
        if (odas_timestamp_ == 0U) {
            ++odas_timestamp_;
        }

        for (std::size_t microphone = 0; microphone < input_channels_.size(); ++microphone) {
            const auto channel = audio.channel(input_channels_[microphone]);
            for (std::size_t sample = 0; sample < channel.size(); ++sample) {
                hops_in_->hops->array[microphone][sample] = channel[sample];
            }
        }
        hops_in_->timeStamp = odas_timestamp_;
        hops_in_->fS = options_.sample_rate_hz;

        processOrThrow(mod_stft_process(stft_), "ODAS STFT processing failed");
        processOrThrow(mod_ssl_process(ssl_), "ODAS SSL processing failed");

        targets_zero(targets_->targets);
        targets_->timeStamp = odas_timestamp_;
        targets_->fS = options_.sample_rate_hz;
        processOrThrow(mod_sst_process(sst_), "ODAS SST processing failed");

        msg_hops_obj* selected_hops = nullptr;
        if (options_.sss.enabled) {
            if (options_.sss.output == OdasSeparatedOutput::Postfiltered) {
                processOrThrow(mod_noise_process(noise_), "ODAS noise estimation failed");
                processOrThrow(mod_sss_process(sss_),
                               "ODAS source separation/post-filter processing failed");
                processOrThrow(mod_istft_process(istft_postfiltered_),
                               "ODAS post-filter ISTFT failed");
                selected_hops = postfiltered_hops_;
            } else {
                powers_->timeStamp = odas_timestamp_;
                powers_->fS = options_.sample_rate_hz;
                const int status = options_.sss.separation_mode == OdasSeparationMode::DelayAndSum
                                       ? mod_sss_process_dds(sss_)
                                       : mod_sss_process_dgss(sss_);
                processOrThrow(status, "ODAS source separation failed");
                processOrThrow(mod_istft_process(istft_separated_),
                               "ODAS separated ISTFT failed");
                selected_hops = separated_hops_;
            }
        }

        return collectResult(selected_hops);
    }

private:
    void validateInput(AudioView audio) const {
        if (audio.format().sample_rate_hz != options_.sample_rate_hz) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "ODAS input sample rate does not match configured sample rate"};
        }
        if (audio.frameCount() != static_cast<std::size_t>(options_.hop_size)) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "ODAS input must contain exactly one configured hop"};
        }
        const auto required_channels =
            *std::max_element(input_channels_.begin(), input_channels_.end()) + 1U;
        if (audio.format().channel_count < required_channels) {
            throw Error{ErrorCode::UnsupportedFormat,
                        "ODAS input does not contain all configured microphone channels"};
        }
    }

    [[nodiscard]] OdasRuntimeResult collectResult(const msg_hops_obj* separated) const {
        OdasRuntimeResult result{};
        result.active_slots.reserve(options_.sst.max_tracks);
        result.live_tracker_ids.reserve(options_.sst.max_tracks);

        for (std::size_t slot = 0; slot < static_cast<std::size_t>(options_.sst.max_tracks); ++slot) {
            const auto live_id = static_cast<std::uint64_t>(sst_->ids[slot]);
            if (live_id != 0U) {
                result.live_tracker_ids.push_back(live_id);
            }

            const auto output_id = static_cast<std::uint64_t>(tracks_->tracks->ids[slot]);
            if (output_id == 0U) {
                continue;
            }

            OdasSlotResult output{};
            output.track_id = output_id;
            output.direction = {
                static_cast<double>(tracks_->tracks->array[slot * 3U + 0U]),
                static_cast<double>(tracks_->tracks->array[slot * 3U + 1U]),
                static_cast<double>(tracks_->tracks->array[slot * 3U + 2U]),
            };
            output.activity = static_cast<double>(tracks_->tracks->activity[slot]);

            // SST and SSS use the same fixed track-slot index. Never compact active
            // tracks before associating a separated channel with its track ID.
            if (separated != nullptr) {
                std::vector<float> samples(static_cast<std::size_t>(options_.hop_size));
                for (std::size_t sample = 0; sample < samples.size(); ++sample) {
                    samples[sample] = separated->hops->array[slot][sample];
                }
                output.separated_audio = std::move(samples);
            }
            result.active_slots.push_back(std::move(output));
        }
        return result;
    }

    void rebuild() {
        const auto microphone_count = static_cast<unsigned int>(options_.microphone_array.size());
        const auto half_frame_size = options_.frame_size / 2U + 1U;

        const msg_hops_cfg hops_cfg{options_.hop_size, microphone_count, options_.sample_rate_hz};
        const msg_spectra_cfg spectra_cfg{half_frame_size, microphone_count, options_.sample_rate_hz};
        const msg_pots_cfg pots_cfg{options_.ssl.potential_source_count, options_.sample_rate_hz};
        const msg_targets_cfg targets_cfg{0U, options_.sample_rate_hz};
        const msg_tracks_cfg tracks_cfg{options_.sst.max_tracks, options_.sample_rate_hz};

        OdasUniquePtr<mod_stft_cfg, mod_stft_cfg_destroy> stft_cfg{
            requireAllocated(mod_stft_cfg_construct())};
        OdasUniquePtr<mod_ssl_cfg, mod_ssl_cfg_destroy> ssl_cfg{makeSslConfig(options_)};
        OdasUniquePtr<mod_sst_cfg, mod_sst_cfg_destroy> sst_cfg{makeSstConfig(options_)};

        try {
            hops_in_ = requireAllocated(msg_hops_construct(&hops_cfg));
            spectra_ = requireAllocated(msg_spectra_construct(&spectra_cfg));
            pots_ = requireAllocated(msg_pots_construct(&pots_cfg));
            targets_ = requireAllocated(msg_targets_construct(&targets_cfg));
            tracks_ = requireAllocated(msg_tracks_construct(&tracks_cfg));

            stft_ = requireAllocated(mod_stft_construct(stft_cfg.get(), &hops_cfg, &spectra_cfg));
            ssl_ = requireAllocated(mod_ssl_construct(ssl_cfg.get(), &spectra_cfg, &pots_cfg));
            sst_ = requireAllocated(mod_sst_construct(sst_cfg.get(), ssl_cfg.get(), &pots_cfg,
                                                      &targets_cfg, &tracks_cfg));

            mod_stft_connect(stft_, hops_in_, spectra_);
            mod_ssl_connect(ssl_, spectra_, pots_);
            mod_sst_connect(sst_, pots_, targets_, tracks_);
            mod_stft_enable(stft_);
            mod_ssl_enable(ssl_);
            mod_sst_enable(sst_);

            if (options_.sss.enabled) {
                buildSeparationGraph(half_frame_size, microphone_count, tracks_cfg, spectra_cfg);
            }
        } catch (...) {
            destroyRuntime();
            throw;
        }
    }

    void buildSeparationGraph(unsigned int half_frame_size, unsigned int microphone_count,
                              const msg_tracks_cfg& tracks_cfg,
                              const msg_spectra_cfg& spectra_cfg) {
        const msg_powers_cfg powers_cfg{half_frame_size, microphone_count, options_.sample_rate_hz};
        const msg_spectra_cfg separated_spectra_cfg{
            half_frame_size, options_.sst.max_tracks, options_.sample_rate_hz};
        const msg_hops_cfg separated_hops_cfg{
            options_.hop_size, options_.sst.max_tracks, options_.sample_rate_hz};

        OdasUniquePtr<mod_sss_cfg, mod_sss_cfg_destroy> sss_cfg{makeSssConfig(options_)};
        powers_ = requireAllocated(msg_powers_construct(&powers_cfg));
        separated_spectra_ = requireAllocated(msg_spectra_construct(&separated_spectra_cfg));
        postfiltered_spectra_ = requireAllocated(msg_spectra_construct(&separated_spectra_cfg));
        separated_hops_ = requireAllocated(msg_hops_construct(&separated_hops_cfg));
        postfiltered_hops_ = requireAllocated(msg_hops_construct(&separated_hops_cfg));
        sss_ = requireAllocated(mod_sss_construct(sss_cfg.get(), &tracks_cfg, &spectra_cfg));
        mod_sss_connect(sss_, spectra_, powers_, tracks_, separated_spectra_, postfiltered_spectra_);
        mod_sss_enable(sss_);

        OdasUniquePtr<mod_istft_cfg, mod_istft_cfg_destroy> istft_cfg{
            requireAllocated(mod_istft_cfg_construct())};
        if (options_.sss.output == OdasSeparatedOutput::Postfiltered) {
            OdasUniquePtr<mod_noise_cfg, mod_noise_cfg_destroy> noise_cfg{makeNoiseConfig(options_)};
            noise_ = requireAllocated(mod_noise_construct(noise_cfg.get(), &spectra_cfg, &powers_cfg));
            istft_postfiltered_ = requireAllocated(
                mod_istft_construct(istft_cfg.get(), &separated_spectra_cfg, &separated_hops_cfg));
            mod_noise_connect(noise_, spectra_, powers_);
            mod_istft_connect(istft_postfiltered_, postfiltered_spectra_, postfiltered_hops_);
            mod_noise_enable(noise_);
            mod_istft_enable(istft_postfiltered_);
        } else {
            istft_separated_ = requireAllocated(
                mod_istft_construct(istft_cfg.get(), &separated_spectra_cfg, &separated_hops_cfg));
            mod_istft_connect(istft_separated_, separated_spectra_, separated_hops_);
            mod_istft_enable(istft_separated_);
        }
    }

    void destroyRuntime() noexcept {
        if (istft_postfiltered_ != nullptr) {
            mod_istft_destroy(istft_postfiltered_);
            istft_postfiltered_ = nullptr;
        }
        if (istft_separated_ != nullptr) {
            mod_istft_destroy(istft_separated_);
            istft_separated_ = nullptr;
        }
        if (sss_ != nullptr) {
            mod_sss_destroy(sss_);
            sss_ = nullptr;
        }
        if (sst_ != nullptr) {
            mod_sst_destroy(sst_);
            sst_ = nullptr;
        }
        if (ssl_ != nullptr) {
            mod_ssl_destroy(ssl_);
            ssl_ = nullptr;
        }
        if (noise_ != nullptr) {
            mod_noise_destroy(noise_);
            noise_ = nullptr;
        }
        if (stft_ != nullptr) {
            mod_stft_destroy(stft_);
            stft_ = nullptr;
        }

        if (postfiltered_hops_ != nullptr) {
            msg_hops_destroy(postfiltered_hops_);
            postfiltered_hops_ = nullptr;
        }
        if (separated_hops_ != nullptr) {
            msg_hops_destroy(separated_hops_);
            separated_hops_ = nullptr;
        }
        if (postfiltered_spectra_ != nullptr) {
            msg_spectra_destroy(postfiltered_spectra_);
            postfiltered_spectra_ = nullptr;
        }
        if (separated_spectra_ != nullptr) {
            msg_spectra_destroy(separated_spectra_);
            separated_spectra_ = nullptr;
        }
        if (tracks_ != nullptr) {
            msg_tracks_destroy(tracks_);
            tracks_ = nullptr;
        }
        if (targets_ != nullptr) {
            msg_targets_destroy(targets_);
            targets_ = nullptr;
        }
        if (pots_ != nullptr) {
            msg_pots_destroy(pots_);
            pots_ = nullptr;
        }
        if (powers_ != nullptr) {
            msg_powers_destroy(powers_);
            powers_ = nullptr;
        }
        if (spectra_ != nullptr) {
            msg_spectra_destroy(spectra_);
            spectra_ = nullptr;
        }
        if (hops_in_ != nullptr) {
            msg_hops_destroy(hops_in_);
            hops_in_ = nullptr;
        }
    }

    OdasOptions options_{};
    std::vector<std::size_t> input_channels_{};
    std::uint64_t odas_timestamp_{0U};

    msg_hops_obj* hops_in_{nullptr};
    msg_spectra_obj* spectra_{nullptr};
    msg_powers_obj* powers_{nullptr};
    msg_pots_obj* pots_{nullptr};
    msg_targets_obj* targets_{nullptr};
    msg_tracks_obj* tracks_{nullptr};
    msg_spectra_obj* separated_spectra_{nullptr};
    msg_spectra_obj* postfiltered_spectra_{nullptr};
    msg_hops_obj* separated_hops_{nullptr};
    msg_hops_obj* postfiltered_hops_{nullptr};

    mod_stft_obj* stft_{nullptr};
    mod_noise_obj* noise_{nullptr};
    mod_ssl_obj* ssl_{nullptr};
    mod_sst_obj* sst_{nullptr};
    mod_sss_obj* sss_{nullptr};
    mod_istft_obj* istft_separated_{nullptr};
    mod_istft_obj* istft_postfiltered_{nullptr};
};

OdasRuntime::OdasRuntime(const OdasOptions& options) : impl_(std::make_unique<Impl>(options)) {}
OdasRuntime::~OdasRuntime() = default;
OdasRuntime::OdasRuntime(OdasRuntime&&) noexcept = default;
OdasRuntime& OdasRuntime::operator=(OdasRuntime&&) noexcept = default;
void OdasRuntime::reset() { impl_->reset(); }
OdasRuntimeResult OdasRuntime::process(AudioView audio) { return impl_->process(audio); }

}  // namespace audition::odas_detail

#include "detail/odas_config.hpp"

#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include <audition/core/error.hpp>

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

std::vector<OdasMicrophoneDirectivity> resolvedDirectivity(const OdasOptions& options) {
    if (!options.microphone_directivity.empty()) {
        return options.microphone_directivity;
    }
    return std::vector<OdasMicrophoneDirectivity>(options.microphone_array.size());
}

void fillMics(mics_obj* destination, const OdasOptions& options) {
    const auto directivity = resolvedDirectivity(options);
    for (std::size_t channel = 0; channel < options.microphone_array.size(); ++channel) {
        const auto& microphone = options.microphone_array.microphones[channel];
        const auto direction = microphone.direction.vector();
        const std::size_t xyz = channel * 3U;
        const std::size_t covariance = channel * 9U;

        destination->mu[xyz + 0U] = static_cast<float>(microphone.position_m.x);
        destination->mu[xyz + 1U] = static_cast<float>(microphone.position_m.y);
        destination->mu[xyz + 2U] = static_cast<float>(microphone.position_m.z);
        for (std::size_t i = 0; i < 9U; ++i) {
            destination->sigma2[covariance + i] =
                static_cast<float>(microphone.position_covariance_m2[i]);
        }
        destination->direction[xyz + 0U] = static_cast<float>(direction.x);
        destination->direction[xyz + 1U] = static_cast<float>(direction.y);
        destination->direction[xyz + 2U] = static_cast<float>(direction.z);
        destination->thetaAllPass[channel] =
            static_cast<float>(directivity[channel].all_pass_angle_deg);
        destination->thetaNoPass[channel] =
            static_cast<float>(directivity[channel].no_pass_angle_deg);
    }
}

void fillSpatialFilters(spatialfilters_obj* destination, const OdasOptions& options) {
    for (std::size_t i = 0; i < options.spatial_filters.size(); ++i) {
        const auto& filter = options.spatial_filters[i];
        const auto direction = filter.direction.vector();
        const std::size_t xyz = i * 3U;
        destination->direction[xyz + 0U] = static_cast<float>(direction.x);
        destination->direction[xyz + 1U] = static_cast<float>(direction.y);
        destination->direction[xyz + 2U] = static_cast<float>(direction.z);
        destination->thetaAllPass[i] = static_cast<float>(filter.all_pass_angle_deg);
        destination->thetaNoPass[i] = static_cast<float>(filter.no_pass_angle_deg);
    }
}

char trackingMode(OdasTrackingFilter value) {
    switch (value) {
        case OdasTrackingFilter::Kalman:
            return 'k';
        case OdasTrackingFilter::Particle:
            return 'p';
    }
    throw Error{ErrorCode::ConfigurationError, "Unsupported ODAS tracking filter"};
}

char additionMode(OdasTrackAddition value) {
    switch (value) {
        case OdasTrackAddition::Dynamic:
            return 'd';
        case OdasTrackAddition::Static:
            return 's';
    }
    throw Error{ErrorCode::ConfigurationError, "Unsupported ODAS track-addition mode"};
}

char separationMode(OdasSeparationMode value) {
    switch (value) {
        case OdasSeparationMode::DelayAndSum:
            return 'd';
        case OdasSeparationMode::GeometricSourceSeparation:
            return 'g';
    }
    throw Error{ErrorCode::ConfigurationError, "Unsupported ODAS separation mode"};
}

char postFilterMode(OdasPostFilterMode value) {
    switch (value) {
        case OdasPostFilterMode::MultiSource:
            return 'm';
        case OdasPostFilterMode::SingleSource:
            return 's';
    }
    throw Error{ErrorCode::ConfigurationError, "Unsupported ODAS post-filter mode"};
}

}  // namespace

mod_ssl_cfg* makeSslConfig(const OdasOptions& options) {
    OdasUniquePtr<mod_ssl_cfg, mod_ssl_cfg_destroy> cfg{requireAllocated(mod_ssl_cfg_construct())};
    cfg->mics = requireAllocated(mics_construct_zero(
        static_cast<unsigned int>(options.microphone_array.size())));
    fillMics(cfg->mics, options);
    cfg->samplerate = requireAllocated(samplerate_construct_zero());
    cfg->samplerate->mu = options.sample_rate_hz;
    cfg->samplerate->sigma2 = static_cast<float>(options.sample_rate_variance);
    cfg->soundspeed = requireAllocated(soundspeed_construct_zero());
    cfg->soundspeed->mu = static_cast<float>(options.speed_of_sound_mps);
    cfg->soundspeed->sigma2 = static_cast<float>(options.speed_of_sound_variance);
    cfg->spatialfilters = requireAllocated(
        spatialfilters_construct_zero(static_cast<unsigned int>(options.spatial_filters.size())));
    fillSpatialFilters(cfg->spatialfilters, options);
    cfg->interpRate = options.ssl.interpolation_rate;
    cfg->epsilon = static_cast<float>(options.epsilon);
    cfg->nLevels = static_cast<unsigned int>(options.ssl.scan_levels.size());
    cfg->levels = static_cast<unsigned int*>(std::malloc(sizeof(unsigned int) * cfg->nLevels));
    cfg->deltas = static_cast<signed int*>(std::malloc(sizeof(signed int) * cfg->nLevels));
    if (cfg->levels == nullptr || cfg->deltas == nullptr) {
        throw std::bad_alloc{};
    }
    for (std::size_t i = 0; i < options.ssl.scan_levels.size(); ++i) {
        cfg->levels[i] = options.ssl.scan_levels[i].level;
        cfg->deltas[i] = options.ssl.scan_levels[i].delta;
    }
    cfg->nMatches = options.ssl.match_count;
    cfg->probMin = static_cast<float>(options.ssl.minimum_probability);
    cfg->nRefinedLevels = options.ssl.refined_level_count;
    cfg->nThetas = options.directivity_theta_count;
    cfg->gainMin = static_cast<float>(options.minimum_microphone_pair_gain);
    return cfg.release();
}

mod_sst_cfg* makeSstConfig(const OdasOptions& options) {
    OdasUniquePtr<mod_sst_cfg, mod_sst_cfg_destroy> cfg{requireAllocated(mod_sst_cfg_construct())};
    cfg->mode = trackingMode(options.sst.filter);
    cfg->add = additionMode(options.sst.addition);
    cfg->nTracksMax = options.sst.max_tracks;
    cfg->hopSize = options.hop_size;
    cfg->sigmaQ = static_cast<float>(options.sst.kalman_sigma_q);
    cfg->nParticles = options.sst.particle_count;
    cfg->st_alpha = static_cast<float>(options.sst.particle_st_alpha);
    cfg->st_beta = static_cast<float>(options.sst.particle_st_beta);
    cfg->st_ratio = static_cast<float>(options.sst.particle_st_ratio);
    cfg->ve_alpha = static_cast<float>(options.sst.particle_ve_alpha);
    cfg->ve_beta = static_cast<float>(options.sst.particle_ve_beta);
    cfg->ve_ratio = static_cast<float>(options.sst.particle_ve_ratio);
    cfg->ac_alpha = static_cast<float>(options.sst.particle_ac_alpha);
    cfg->ac_beta = static_cast<float>(options.sst.particle_ac_beta);
    cfg->ac_ratio = static_cast<float>(options.sst.particle_ac_ratio);
    cfg->Nmin = static_cast<float>(options.sst.particle_n_min);
    cfg->epsilon = static_cast<float>(options.epsilon);
    cfg->sigmaR_prob = static_cast<float>(std::sqrt(options.sst.sigma_r2_probability));
    cfg->sigmaR_active = static_cast<float>(std::sqrt(options.sst.sigma_r2_active));
    cfg->sigmaR_target = static_cast<float>(std::sqrt(options.sst.sigma_r2_target));

    cfg->active_gmm = requireAllocated(
        gaussians_1d_construct_null(static_cast<unsigned int>(options.sst.active_gmm.size())));
    for (std::size_t i = 0; i < options.sst.active_gmm.size(); ++i) {
        const auto& component = options.sst.active_gmm[i];
        cfg->active_gmm->array[i] = requireAllocated(gaussian_1d_construct_weightmusigma(
            static_cast<float>(component.weight), static_cast<float>(component.mean),
            static_cast<float>(std::sqrt(component.variance))));
    }
    cfg->inactive_gmm = requireAllocated(
        gaussians_1d_construct_null(static_cast<unsigned int>(options.sst.inactive_gmm.size())));
    for (std::size_t i = 0; i < options.sst.inactive_gmm.size(); ++i) {
        const auto& component = options.sst.inactive_gmm[i];
        cfg->inactive_gmm->array[i] = requireAllocated(gaussian_1d_construct_weightmusigma(
            static_cast<float>(component.weight), static_cast<float>(component.mean),
            static_cast<float>(std::sqrt(component.variance))));
    }

    cfg->Pfalse = static_cast<float>(options.sst.false_probability);
    cfg->Pnew = static_cast<float>(options.sst.new_probability);
    cfg->Ptrack = static_cast<float>(options.sst.track_probability);
    cfg->theta_new = static_cast<float>(options.sst.new_threshold);
    cfg->N_prob = options.sst.probability_window;
    cfg->theta_prob = static_cast<float>(options.sst.probability_threshold);
    cfg->theta_inactive = static_cast<float>(options.sst.inactive_threshold);
    cfg->N_inactive = static_cast<unsigned int*>(
        std::malloc(sizeof(unsigned int) * static_cast<std::size_t>(cfg->nTracksMax)));
    if (cfg->N_inactive == nullptr) {
        throw std::bad_alloc{};
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(cfg->nTracksMax); ++i) {
        cfg->N_inactive[i] = options.sst.inactive_timeout_hops_per_track.empty()
                                 ? options.sst.inactive_timeout_hops
                                 : options.sst.inactive_timeout_hops_per_track[i];
    }
    return cfg.release();
}

mod_noise_cfg* makeNoiseConfig(const OdasOptions& options) {
    OdasUniquePtr<mod_noise_cfg, mod_noise_cfg_destroy> cfg{
        requireAllocated(mod_noise_cfg_construct())};
    cfg->bSize = options.noise.smoothing_window;
    cfg->alphaS = static_cast<float>(options.noise.alpha_s);
    cfg->L = options.noise.frame_count;
    cfg->delta = static_cast<float>(options.noise.delta);
    cfg->alphaD = static_cast<float>(options.noise.alpha_d);
    return cfg.release();
}

mod_sss_cfg* makeSssConfig(const OdasOptions& options) {
    OdasUniquePtr<mod_sss_cfg, mod_sss_cfg_destroy> cfg{requireAllocated(mod_sss_cfg_construct())};
    cfg->mode_sep = separationMode(options.sss.separation_mode);
    cfg->mode_pf = postFilterMode(options.sss.post_filter_mode);
    cfg->nThetas = options.directivity_theta_count;
    cfg->gainMin = static_cast<float>(options.minimum_microphone_pair_gain);
    cfg->epsilon = static_cast<float>(options.epsilon);
    cfg->mics = requireAllocated(
        mics_construct_zero(static_cast<unsigned int>(options.microphone_array.size())));
    fillMics(cfg->mics, options);
    cfg->samplerate = requireAllocated(samplerate_construct_zero());
    cfg->samplerate->mu = options.sample_rate_hz;
    cfg->samplerate->sigma2 = static_cast<float>(options.sample_rate_variance);
    cfg->soundspeed = requireAllocated(soundspeed_construct_zero());
    cfg->soundspeed->mu = static_cast<float>(options.speed_of_sound_mps);
    cfg->soundspeed->sigma2 = static_cast<float>(options.speed_of_sound_variance);
    cfg->sep_gss_mu = static_cast<float>(options.sss.dgss_mu);
    cfg->sep_gss_lambda = static_cast<float>(options.sss.dgss_lambda);
    cfg->pf_ms_bSize = options.noise.smoothing_window;
    cfg->pf_ms_alphaS = static_cast<float>(options.noise.alpha_s);
    cfg->pf_ms_L = options.noise.frame_count;
    cfg->pf_ms_delta = static_cast<float>(options.noise.delta);
    cfg->pf_ms_alphaD = static_cast<float>(options.noise.alpha_d);
    cfg->pf_ms_eta = static_cast<float>(options.sss.ms_eta);
    cfg->pf_ms_alphaZ = static_cast<float>(options.sss.ms_alpha_z);
    cfg->pf_ms_alphaPmin = static_cast<float>(options.sss.ms_alpha_p_min);
    cfg->pf_ms_thetaWin = static_cast<float>(options.sss.ms_theta_win);
    cfg->pf_ms_alphaWin = static_cast<float>(options.sss.ms_alpha_win);
    cfg->pf_ms_maxAbsenceProb = static_cast<float>(options.sss.ms_max_absence_probability);
    cfg->pf_ms_Gmin = static_cast<float>(options.sss.ms_min_gain);
    cfg->pf_ms_winSizeLocal = options.sss.ms_local_window;
    cfg->pf_ms_winSizeGlobal = options.sss.ms_global_window;
    cfg->pf_ms_winSizeFrame = options.sss.ms_frame_window;
    cfg->pf_ss_Gmin = static_cast<float>(options.sss.ss_min_gain);
    cfg->pf_ss_Gmid = static_cast<float>(options.sss.ss_mid_gain);
    cfg->pf_ss_Gslope = static_cast<float>(options.sss.ss_slope);
    return cfg.release();
}

}  // namespace audition::odas_detail

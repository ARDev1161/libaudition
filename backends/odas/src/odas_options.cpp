#include <audition/backends/odas/odas_spatial_engine.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <audition/core/error.hpp>

#include "detail/odas_options_detail.hpp"

namespace audition {
namespace {

template <typename T>
bool finite(T value) {
    return std::isfinite(static_cast<double>(value));
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw Error{ErrorCode::ConfigurationError, message};
    }
}

void requireProbability(double value, const char* message) {
    require(finite(value) && value >= 0.0 && value <= 1.0, message);
}

void requireNonNegative(double value, const char* message) {
    require(finite(value) && value >= 0.0, message);
}

void requirePositive(double value, const char* message) {
    require(finite(value) && value > 0.0, message);
}

void validateAngles(double all_pass, double no_pass, const char* message) {
    require(finite(all_pass) && finite(no_pass) && all_pass >= 0.0 && no_pass >= 0.0 &&
                all_pass <= no_pass && no_pass <= 180.0,
            message);
}

void validateMicrophoneGeometry(const MicrophoneGeometry& microphone) {
    require(finite(microphone.position_m.x) && finite(microphone.position_m.y) &&
                finite(microphone.position_m.z),
            "ODAS microphone positions must be finite");
    for (const double value : microphone.position_covariance_m2) {
        require(finite(value), "ODAS microphone position covariance must be finite");
    }
    requireNonNegative(microphone.position_covariance_m2[0],
                       "ODAS microphone X position variance must be non-negative");
    requireNonNegative(microphone.position_covariance_m2[4],
                       "ODAS microphone Y position variance must be non-negative");
    requireNonNegative(microphone.position_covariance_m2[8],
                       "ODAS microphone Z position variance must be non-negative");
}

}  // namespace

namespace odas_detail {

std::vector<std::size_t> resolvedInputChannels(const OdasOptions& options) {
    if (!options.input_channels.empty()) {
        return options.input_channels;
    }
    std::vector<std::size_t> channels(options.microphone_array.size());
    for (std::size_t i = 0; i < channels.size(); ++i) {
        channels[i] = i;
    }
    return channels;
}

}  // namespace odas_detail

void validateOdasOptions(const OdasOptions& options) {
    require(options.microphone_array.size() >= 2U, "ODAS requires at least two microphones");
    require(options.microphone_array.size() <=
                static_cast<std::size_t>(std::numeric_limits<unsigned int>::max()),
            "ODAS microphone count exceeds supported range");
    for (const auto& microphone : options.microphone_array.microphones) {
        validateMicrophoneGeometry(microphone);
    }

    require(options.sample_rate_hz > 0U, "ODAS sample rate must be non-zero");
    require(options.hop_size > 0U, "ODAS hop size must be non-zero");
    require(options.frame_size >= 2U && options.frame_size % 2U == 0U,
            "ODAS frame size must be an even value of at least two samples");
    require(options.hop_size <= options.frame_size, "ODAS hop size must not exceed frame size");
    require(options.directivity_theta_count >= 2U,
            "ODAS directivity theta count must be at least two");
    requirePositive(options.speed_of_sound_mps, "ODAS speed of sound must be positive");
    requireNonNegative(options.speed_of_sound_variance,
                       "ODAS speed-of-sound variance must be non-negative");
    requireNonNegative(options.sample_rate_variance,
                       "ODAS sample-rate variance must be non-negative");
    requirePositive(options.epsilon, "ODAS epsilon must be positive");
    requireProbability(options.minimum_microphone_pair_gain,
                       "ODAS minimum microphone-pair gain must be in [0, 1]");

    const auto input_channels = odas_detail::resolvedInputChannels(options);
    require(input_channels.size() == options.microphone_array.size(),
            "ODAS input-channel mapping must contain exactly one entry per microphone");
    auto sorted_channels = input_channels;
    std::sort(sorted_channels.begin(), sorted_channels.end());
    require(std::adjacent_find(sorted_channels.begin(), sorted_channels.end()) ==
                sorted_channels.end(),
            "ODAS input-channel mapping must not contain duplicates");
    require(sorted_channels.back() <
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
            "ODAS input-channel index exceeds supported range");

    require(options.microphone_directivity.empty() ||
                options.microphone_directivity.size() == options.microphone_array.size(),
            "ODAS microphone directivity must be empty or contain one entry per microphone");
    if (!options.microphone_directivity.empty()) {
        for (const auto& directivity : options.microphone_directivity) {
            validateAngles(directivity.all_pass_angle_deg, directivity.no_pass_angle_deg,
                           "ODAS microphone directivity angles must satisfy 0 <= all-pass <= no-pass <= 180");
        }
    }
    for (const auto& filter : options.spatial_filters) {
        validateAngles(filter.all_pass_angle_deg, filter.no_pass_angle_deg,
                       "ODAS spatial-filter angles must satisfy 0 <= all-pass <= no-pass <= 180");
    }

    require(options.ssl.potential_source_count > 0U,
            "ODAS SSL potential-source count must be non-zero");
    require(options.ssl.interpolation_rate > 0U,
            "ODAS SSL interpolation rate must be non-zero");
    require(!options.ssl.scan_levels.empty(), "ODAS SSL requires at least one scan level");
    require(options.ssl.potential_source_count >= options.ssl.scan_levels.size(),
            "ODAS SSL potential-source count must be at least the scan-level count");
    for (const auto& scan : options.ssl.scan_levels) {
        require(scan.level > 0U, "ODAS SSL scan levels must be non-zero");
        require(scan.delta >= -1, "ODAS SSL scan delta must be -1 or non-negative");
    }
    require(options.ssl.match_count > 0U, "ODAS SSL match count must be non-zero");
    requireProbability(options.ssl.minimum_probability,
                       "ODAS SSL minimum probability must be in [0, 1]");
    require(options.ssl.refined_level_count <= options.ssl.scan_levels.size(),
            "ODAS SSL refined-level count must not exceed scan-level count");

    require(options.sst.max_tracks > 0U, "ODAS SST maximum track count must be non-zero");
    requirePositive(options.sst.kalman_sigma_q, "ODAS SST Kalman sigma Q must be positive");
    require(options.sst.particle_count > 0U, "ODAS SST particle count must be non-zero");
    requirePositive(options.sst.sigma_r2_probability,
                    "ODAS SST probability variance must be positive");
    requirePositive(options.sst.sigma_r2_active, "ODAS SST active variance must be positive");
    requirePositive(options.sst.sigma_r2_target, "ODAS SST target variance must be positive");
    requirePositive(options.sst.particle_st_alpha, "ODAS SST particle st_alpha must be positive");
    requirePositive(options.sst.particle_st_beta, "ODAS SST particle st_beta must be positive");
    requireProbability(options.sst.particle_st_ratio,
                       "ODAS SST particle st_ratio must be in [0, 1]");
    requirePositive(options.sst.particle_ve_alpha, "ODAS SST particle ve_alpha must be positive");
    requirePositive(options.sst.particle_ve_beta, "ODAS SST particle ve_beta must be positive");
    requireProbability(options.sst.particle_ve_ratio,
                       "ODAS SST particle ve_ratio must be in [0, 1]");
    requirePositive(options.sst.particle_ac_alpha, "ODAS SST particle ac_alpha must be positive");
    requirePositive(options.sst.particle_ac_beta, "ODAS SST particle ac_beta must be positive");
    requireProbability(options.sst.particle_ac_ratio,
                       "ODAS SST particle ac_ratio must be in [0, 1]");
    requireProbability(options.sst.particle_n_min, "ODAS SST particle Nmin must be in [0, 1]");
    require(!options.sst.active_gmm.empty(), "ODAS SST active GMM must not be empty");
    require(!options.sst.inactive_gmm.empty(), "ODAS SST inactive GMM must not be empty");
    for (const auto& component : options.sst.active_gmm) {
        requireNonNegative(component.weight, "ODAS SST active GMM weights must be non-negative");
        require(finite(component.mean), "ODAS SST active GMM means must be finite");
        requirePositive(component.variance, "ODAS SST active GMM variances must be positive");
    }
    for (const auto& component : options.sst.inactive_gmm) {
        requireNonNegative(component.weight,
                           "ODAS SST inactive GMM weights must be non-negative");
        require(finite(component.mean), "ODAS SST inactive GMM means must be finite");
        requirePositive(component.variance, "ODAS SST inactive GMM variances must be positive");
    }
    requireProbability(options.sst.false_probability,
                       "ODAS SST false probability must be in [0, 1]");
    requireProbability(options.sst.new_probability,
                       "ODAS SST new probability must be in [0, 1]");
    requireProbability(options.sst.track_probability,
                       "ODAS SST track probability must be in [0, 1]");
    requireProbability(options.sst.new_threshold, "ODAS SST new threshold must be in [0, 1]");
    requireProbability(options.sst.probability_threshold,
                       "ODAS SST probability threshold must be in [0, 1]");
    requireProbability(options.sst.inactive_threshold,
                       "ODAS SST inactive threshold must be in [0, 1]");
    require(options.sst.probability_window > 0U, "ODAS SST probability window must be non-zero");
    require(options.sst.inactive_timeout_hops > 0U ||
                !options.sst.inactive_timeout_hops_per_track.empty(),
            "ODAS SST inactive timeout must be configured");
    require(options.sst.inactive_timeout_hops_per_track.empty() ||
                options.sst.inactive_timeout_hops_per_track.size() == options.sst.max_tracks,
            "ODAS SST per-track inactive timeouts must match max_tracks");
    for (const auto timeout : options.sst.inactive_timeout_hops_per_track) {
        require(timeout > 0U, "ODAS SST per-track inactive timeouts must be non-zero");
    }

    if (options.sss.enabled) {
        require(options.noise.smoothing_window > 0U,
                "ODAS noise smoothing window must be non-zero");
        require(options.noise.frame_count > 0U, "ODAS noise frame count must be non-zero");
        requireProbability(options.noise.alpha_s, "ODAS noise alpha S must be in [0, 1]");
        requireNonNegative(options.noise.delta, "ODAS noise delta must be non-negative");
        requireProbability(options.noise.alpha_d, "ODAS noise alpha D must be in [0, 1]");

        requireNonNegative(options.sss.dgss_mu, "ODAS DGSS mu must be non-negative");
        requireNonNegative(options.sss.dgss_lambda, "ODAS DGSS lambda must be non-negative");
        requireProbability(options.sss.ms_eta, "ODAS multi-source eta must be in [0, 1]");
        requireProbability(options.sss.ms_alpha_z,
                           "ODAS multi-source alpha Z must be in [0, 1]");
        requireProbability(options.sss.ms_alpha_p_min,
                           "ODAS multi-source alpha Pmin must be in [0, 1]");
        requireProbability(options.sss.ms_theta_win,
                           "ODAS multi-source theta window must be in [0, 1]");
        requireProbability(options.sss.ms_alpha_win,
                           "ODAS multi-source alpha window must be in [0, 1]");
        requireProbability(options.sss.ms_max_absence_probability,
                           "ODAS post-filter absence probability must be in [0, 1]");
        requireProbability(options.sss.ms_min_gain,
                           "ODAS multi-source minimum gain must be in [0, 1]");
        require(options.sss.ms_local_window > 0U && options.sss.ms_global_window > 0U &&
                    options.sss.ms_frame_window > 0U,
                "ODAS post-filter window sizes must be non-zero");
        requireProbability(options.sss.ss_min_gain,
                           "ODAS single-source minimum gain must be in [0, 1]");
        requireProbability(options.sss.ss_mid_gain,
                           "ODAS single-source mid gain must be in [0, 1]");
        requirePositive(options.sss.ss_slope, "ODAS single-source slope must be positive");
    }
}

}  // namespace audition

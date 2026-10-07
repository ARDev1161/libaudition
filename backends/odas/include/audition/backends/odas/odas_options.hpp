#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <audition/core/geometry.hpp>
#include <audition/spatial/microphone_array.hpp>

namespace audition {

struct OdasMicrophoneDirectivity {
    double all_pass_angle_deg{180.0};
    double no_pass_angle_deg{180.0};
};

struct OdasSpatialFilter {
    Direction3D direction{Direction3D::fromVector({0.0, 0.0, 1.0})};
    double all_pass_angle_deg{180.0};
    double no_pass_angle_deg{180.0};
};

struct OdasScanLevel {
    std::uint32_t level{2};
    std::int32_t delta{-1};
};

struct OdasSslOptions {
    std::uint32_t potential_source_count{4};
    std::uint32_t interpolation_rate{4};
    std::vector<OdasScanLevel> scan_levels{{2, -1}, {4, -1}};
    std::uint32_t match_count{10};
    double minimum_probability{0.5};
    std::uint32_t refined_level_count{1};
};

enum class OdasTrackingFilter : std::uint8_t { Kalman, Particle };
enum class OdasTrackAddition : std::uint8_t { Dynamic, Static };

struct OdasGaussianComponent {
    double weight{1.0};
    double mean{0.0};
    double variance{1.0};
};

struct OdasSstOptions {
    OdasTrackingFilter filter{OdasTrackingFilter::Kalman};
    OdasTrackAddition addition{OdasTrackAddition::Dynamic};
    std::uint32_t max_tracks{4};
    double kalman_sigma_q{0.001};
    std::uint32_t particle_count{1000};
    double particle_st_alpha{2.0};
    double particle_st_beta{0.04};
    double particle_st_ratio{0.5};
    double particle_ve_alpha{0.05};
    double particle_ve_beta{0.2};
    double particle_ve_ratio{0.3};
    double particle_ac_alpha{0.5};
    double particle_ac_beta{0.2};
    double particle_ac_ratio{0.2};
    double particle_n_min{0.7};
    double sigma_r2_probability{0.0025};
    double sigma_r2_active{0.0225};
    double sigma_r2_target{0.0025};
    std::vector<OdasGaussianComponent> active_gmm{{1.0, 0.4, 0.0025}};
    std::vector<OdasGaussianComponent> inactive_gmm{{1.0, 0.25, 0.0025}};
    double false_probability{0.1};
    double new_probability{0.1};
    double track_probability{0.8};
    double new_threshold{0.9};
    std::uint32_t probability_window{5};
    double probability_threshold{0.8};
    std::uint32_t inactive_timeout_hops{250};
    std::vector<std::uint32_t> inactive_timeout_hops_per_track{};
    double inactive_threshold{0.9};
};

struct OdasNoiseOptions {
    std::uint32_t smoothing_window{3};
    double alpha_s{0.1};
    std::uint32_t frame_count{150};
    double delta{3.0};
    double alpha_d{0.1};
};

enum class OdasSeparationMode : std::uint8_t { DelayAndSum, GeometricSourceSeparation };
enum class OdasPostFilterMode : std::uint8_t { MultiSource, SingleSource };
enum class OdasSeparatedOutput : std::uint8_t { Separated, Postfiltered };

struct OdasSssOptions {
    bool enabled{true};
    OdasSeparationMode separation_mode{OdasSeparationMode::DelayAndSum};
    OdasPostFilterMode post_filter_mode{OdasPostFilterMode::MultiSource};
    OdasSeparatedOutput output{OdasSeparatedOutput::Separated};
    double dgss_mu{0.01};
    double dgss_lambda{0.5};
    double ms_eta{0.5};
    double ms_alpha_z{0.8};
    double ms_alpha_p_min{0.07};
    double ms_theta_win{0.3};
    double ms_alpha_win{0.3};
    double ms_max_absence_probability{0.9};
    double ms_min_gain{0.01};
    std::uint32_t ms_local_window{3};
    std::uint32_t ms_global_window{23};
    std::uint32_t ms_frame_window{256};
    double ss_min_gain{0.01};
    double ss_mid_gain{0.9};
    double ss_slope{10.0};
};

struct OdasOptions {
    MicrophoneArrayGeometry microphone_array{};
    std::vector<std::size_t> input_channels{};
    std::vector<OdasMicrophoneDirectivity> microphone_directivity{};
    std::vector<OdasSpatialFilter> spatial_filters{{}};
    std::uint32_t sample_rate_hz{16000};
    std::uint32_t hop_size{128};
    std::uint32_t frame_size{256};
    double sample_rate_variance{0.01};
    double speed_of_sound_mps{343.0};
    double speed_of_sound_variance{25.0};
    double epsilon{1.0e-20};
    std::uint32_t directivity_theta_count{181};
    double minimum_microphone_pair_gain{0.25};
    OdasSslOptions ssl{};
    OdasSstOptions sst{};
    OdasNoiseOptions noise{};
    OdasSssOptions sss{};
};

}  // namespace audition

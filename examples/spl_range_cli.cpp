#include <audition/audition.hpp>

#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

double parseDouble(const char* text) {
    std::size_t consumed = 0U;
    const std::string value{text};
    const double result = std::stod(value, &consumed);
    if (consumed != value.size()) {
        throw std::invalid_argument{"not a number"};
    }
    return result;
}

audition::Timestamp ts(std::int64_t nanoseconds) {
    return audition::Timestamp{
        nanoseconds,
        {audition::ClockDomain::Monotonic, 0U}};
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc > 5) {
            std::cerr
                << "usage: libaudition_spl_cli "
                << "[measured_dbfs] [reference_dbfs] "
                << "[reference_spl_db] [source_spl_at_1m_db]\n";
            return 2;
        }

        const double measured_dbfs =
            argc > 1 ? parseDouble(argv[1]) : -40.0;
        const double reference_dbfs =
            argc > 2 ? parseDouble(argv[2]) : -20.0;
        const double reference_spl =
            argc > 3 ? parseDouble(argv[3]) : 94.0;
        const double source_spl =
            argc > 4 ? parseDouble(argv[4]) : 80.0;

        audition::SoundPressureCalibrationProfile profile{};
        profile.profile_id = "cli-reference";
        profile.signal_path_id = "cli-microphone";
        profile.reference_level_db_spl = {reference_spl, 0.25};
        profile.measured_level_dbfs = {reference_dbfs, 0.25};
        profile.reference_frequency_hz = 1000.0;
        profile.weighting = audition::SoundLevelWeighting::Z;
        profile.transfer_variance_db2 = 0.25;

        const audition::SoundPressureLevelCalibrator calibrator{
            profile};

        audition::DbfsLevelObservation dbfs{};
        dbfs.timestamp = ts(0);
        dbfs.level_dbfs = measured_dbfs;
        dbfs.variance_db2 = 0.25;
        dbfs.weighting = audition::SoundLevelWeighting::Z;
        dbfs.signal_path_id = "cli-microphone";

        const auto spl = calibrator.calibrate(dbfs);
        if (!spl.has_value()) {
            std::cerr << "calibration produced no finite SPL observation\n";
            return 1;
        }

        audition::SourceLevelPrior source_priors[] = {
            {
                "cli-source",
                {source_spl, 4.0},
                1.0,
                1.0,
                audition::SoundLevelWeighting::Z,
            },
        };

        audition::RangeEstimationInput range_input{};
        range_input.sound_level = *spl;
        range_input.source_level_priors = source_priors;

        audition::SoundLevelRangePriorOptions range_options{};
        range_options.path_loss_exponent = 2.0;
        range_options.propagation_variance_db2 = 4.0;

        const audition::SoundLevelRangePriorEstimator estimator{
            range_options};
        const auto range = estimator.estimate(range_input);
        if (!range.has_value()) {
            std::cerr << "range prior could not be estimated\n";
            return 1;
        }

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "calibration_offset_db="
                  << calibrator.offsetDb() << '\n';
        std::cout << "spl_db=" << spl->level_db_spl.mean
                  << " variance_db2=" << spl->level_db_spl.variance
                  << '\n';
        std::cout << "range_m=" << range->distance_m.mean
                  << " variance_m2=" << range->distance_m.variance
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}

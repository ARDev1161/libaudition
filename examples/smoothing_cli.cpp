#include <audition/audition.hpp>

#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
        std::vector<double> samples{};
        for (int index = 1; index < argc; ++index) {
            samples.push_back(parseDouble(argv[index]));
        }
        if (samples.empty()) {
            samples = {2.0, 2.4, 1.8, 2.2};
        }

        audition::TemporalSpatialSmootherOptions options{};
        options.range_measurement_weight = 0.35;
        options.range_process_variance_m2_per_s = 0.01;
        audition::TemporalSpatialTrackSmoother smoother{options};

        std::cout << std::fixed << std::setprecision(3);
        for (std::size_t index = 0U; index < samples.size(); ++index) {
            audition::SpatialTrack track{};
            track.track_id = audition::SpatialTrackId{1U};
            track.first_seen = ts(0);
            track.last_seen = ts(
                static_cast<std::int64_t>(index) *
                1'000'000'000LL);
            track.range = audition::RangeEstimate{
                {samples[index], 0.04},
                audition::Probability::zero(),
                audition::RangeEstimate::Method::LevelPrior,
            };

            smoother.update(track);

            std::cout << "sample=" << index
                      << " raw_m=" << samples[index]
                      << " smoothed_m=" << track.range->distance_m.mean
                      << " variance_m2="
                      << track.range->distance_m.variance
                      << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}

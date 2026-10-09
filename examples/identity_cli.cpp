#include <audition/audition.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

namespace {

float parseFloat(const char* text) {
    std::size_t consumed = 0U;
    const std::string value{text};
    const float result = std::stof(value, &consumed);
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

audition::SourceFingerprint fingerprint(float x, float y) {
    audition::SourceFingerprint result{};
    result.model_id = "cli-fingerprint-v1";
    result.embedding = {x, y};
    result.quality = audition::Probability::one();
    return result;
}

audition::SourceIdentityObservation observation(
    std::uint64_t track_id,
    std::int64_t timestamp_ns,
    audition::SourceFingerprint source_fingerprint) {
    audition::SourceIdentityObservation result{};
    result.track_id = audition::SpatialTrackId{track_id};
    result.timestamp = ts(timestamp_ns);
    result.direction.direction =
        audition::Direction3D::fromVector({1.0, 0.0, 0.0});
    result.fingerprint = std::move(source_fingerprint);
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 1 && argc != 5) {
            std::cerr
                << "usage: libaudition_identity_cli "
                << "[first_x first_y second_x second_y]\n";
            return 2;
        }

        const float first_x = argc == 5 ? parseFloat(argv[1]) : 1.0F;
        const float first_y = argc == 5 ? parseFloat(argv[2]) : 0.0F;
        const float second_x = argc == 5 ? parseFloat(argv[3]) : 0.999F;
        const float second_y = argc == 5 ? parseFloat(argv[4]) : 0.01F;

        audition::InMemoryAcousticSourceRegistry registry;
        audition::HeuristicSourceIdentityResolver resolver{registry};

        const auto first = resolver.observe(
            observation(11U, 0, fingerprint(first_x, first_y)));
        resolver.endTrack(audition::SpatialTrackId{11U}, ts(1'000'000'000LL));

        const auto second = resolver.observe(
            observation(
                37U,
                1'100'000'000LL,
                fingerprint(second_x, second_y)));

        const bool reacquired =
            first.source_id == second.source_id &&
            !second.newly_created;

        std::cout << "first_source_id=" << first.source_id.value() << '\n';
        std::cout << "second_source_id=" << second.source_id.value() << '\n';
        std::cout << "association_confidence="
                  << second.confidence.value() << '\n';
        std::cout << "reacquired=" << (reacquired ? "yes" : "no")
                  << '\n';

        return reacquired ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}

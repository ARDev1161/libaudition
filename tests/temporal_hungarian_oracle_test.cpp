#include <audition/temporal/hungarian_event_matcher.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using CostMatrix = std::vector<std::vector<std::optional<double>>>;

// Independent exhaustive reference: select either one unused track or
// unmatched for each observation. Unlike the production Hungarian code,
// this uses recursive enumeration and no potentials/augmenting paths.
double exhaustiveOptimum(const CostMatrix& matrix, std::size_t tracks,
                         double unmatched_penalty) {
    const std::size_t observations = matrix.size();
    double best = std::numeric_limits<double>::infinity();
    std::function<void(std::size_t, std::uint64_t, double)> visit;
    visit = [&](std::size_t row, std::uint64_t used, double cost) {
        if (row == observations) {
            if (cost < best) best = cost;
            return;
        }
        visit(row + 1, used, cost + unmatched_penalty);
        for (std::size_t track = 0; track < tracks; ++track) {
            if ((used & (std::uint64_t{1} << track)) || !matrix[row][track])
                continue;
            visit(row + 1, used | (std::uint64_t{1} << track),
                  cost + *matrix[row][track]);
        }
    };
    visit(0, 0, 0);
    return best;
}

void verifyAssignment(const CostMatrix& matrix, std::size_t tracks,
                      double penalty, const audition::temporal::BatchAssignment& actual) {
    ASSERT_EQ(actual.track_for_observation.size(), matrix.size());
    std::vector<bool> assigned(tracks, false);
    double recomputed = 0.0;
    for (std::size_t row = 0; row < matrix.size(); ++row) {
        const auto track = actual.track_for_observation[row];
        if (!track) {
            recomputed += penalty;
            continue;
        }
        ASSERT_LT(*track, tracks);
        ASSERT_FALSE(assigned[*track]);
        ASSERT_TRUE(matrix[row][*track].has_value());
        EXPECT_LE(*matrix[row][*track], penalty);
        assigned[*track] = true;
        recomputed += *matrix[row][*track];
    }
    std::vector<std::size_t> unmatched;
    for (std::size_t track = 0; track < tracks; ++track)
        if (!assigned[track]) unmatched.push_back(track);
    EXPECT_EQ(actual.unmatched_tracks, unmatched);
    EXPECT_NEAR(actual.total_cost, recomputed, 1e-9);
    EXPECT_NEAR(actual.total_cost, exhaustiveOptimum(matrix, tracks, penalty), 1e-9);
}
}

TEST(HungarianEventMatcherOracle, ExhaustiveSmallMatrixPatterns) {
    audition::temporal::HungarianEventMatcher matcher;
    // All 3^(2*2) possible matrices with forbidden / free / expensive edges.
    // The reference recursion checks that a globally optimal match is chosen.
    for (std::size_t code = 0; code < 81; ++code) {
        std::size_t digits = code;
        CostMatrix matrix(2, std::vector<std::optional<double>>(2));
        for (auto& row : matrix)
            for (auto& cost : row) {
                switch (digits % 3) {
                    case 0: cost = std::nullopt; break;
                    case 1: cost = 0.0; break;
                    default: cost = 9.0; break;
                }
                digits /= 3;
            }
        verifyAssignment(matrix, 2, 5.0, matcher.match(matrix, 2));
    }
}

TEST(HungarianEventMatcherOracle, DeterministicRandomRectangularCases) {
    audition::temporal::HungarianEventMatcher matcher;
    std::mt19937 rng{0xBADC0DEu};
    std::uniform_int_distribution<int> cost(0, 19);
    for (std::size_t observations = 1; observations <= 4; ++observations)
        for (std::size_t tracks = 0; tracks <= 4; ++tracks)
            for (int iteration = 0; iteration < 80; ++iteration) {
                CostMatrix matrix(observations,
                    std::vector<std::optional<double>>(tracks));
                for (auto& row : matrix)
                    for (auto& entry : row) {
                        const int sampled = cost(rng);
                        if (sampled >= 4) entry = static_cast<double>(sampled);
                    }
                verifyAssignment(matrix, tracks, 10.0, matcher.match(matrix, tracks));
            }
}

TEST(HungarianEventMatcherOracle, CapacityAndValidation) {
    audition::temporal::BatchAssignmentOptions opts{};
    opts.max_observations = 1;
    opts.max_tracks = 1;
    audition::temporal::HungarianEventMatcher matcher(opts);
    EXPECT_THROW(static_cast<void>(matcher.match({{}, {}}, 0)), std::length_error);
    EXPECT_THROW(static_cast<void>(matcher.match({}, 2)), std::length_error);
    EXPECT_THROW(static_cast<void>(matcher.match({{std::nullopt}}, 2)), std::length_error);
    EXPECT_THROW(static_cast<void>(matcher.match({{std::numeric_limits<double>::quiet_NaN()}}, 1)),
                 std::invalid_argument);
    const CostMatrix forbidden{{std::nullopt}};
    verifyAssignment(forbidden, 1, 10.0, matcher.match(forbidden, 1));
}

#include <audition/temporal/hungarian_event_matcher.hpp>
#include <audition/temporal/interval_relations.hpp>
#include <audition/temporal/mahalanobis_gate.hpp>
#include <audition/temporal/spatial_temporal_gate.hpp>

#include <audition/core/time.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace audition;
    using namespace audition::temporal;
    try {
        std::string scenario = "baseline";
        double penalty = 2.0;
        for (int i = 1; i < argc; ++i) {
            const std::string argument{argv[i]};
            if (argument == "--scenario" && i + 1 < argc) {
                scenario = argv[++i];
            } else if (argument == "--penalty" && i + 1 < argc) {
                const std::string value{argv[++i]};
                std::size_t consumed = 0;
                penalty = std::stod(value, &consumed);
                if (consumed != value.size())
                    throw std::invalid_argument("invalid penalty");
            } else {
                throw std::invalid_argument("unknown or incomplete argument");
            }
        }
        if (scenario != "baseline" && scenario != "crossed" &&
            scenario != "gated" && scenario != "penalty")
            throw std::invalid_argument("unknown scenario");
        if (!std::isfinite(penalty) || penalty <= 0.0)
            throw std::invalid_argument("invalid penalty");
        if (scenario != "penalty" && penalty != 2.0)
            throw std::invalid_argument("--penalty requires --scenario penalty");

        const auto at=[](std::int64_t time) {
            return Timestamp{time,{ClockDomain::Monotonic,0U}};
        };
        const auto relation=relate({at(0),at(10)},{at(10),at(20)});
        if(relation!=IntervalRelation::Meets)
            throw std::runtime_error("Allen interval relationship");
        std::cout << "interval_relation=meets\n";

        SpatialGateOptions spatialOptions{};
        spatialOptions.max_elapsed=Duration{100};
        spatialOptions.max_distance_m=1.0;
        const auto spatial=SpatialTemporalGate{spatialOptions}.evaluate(
            {at(10),0.0,0.0,0.0},{at(20),0.3,0.0,0.0});
        if(!spatial.compatible)
            throw std::runtime_error("spatial-time gating");
        std::cout << "spatial_gate=accepted distance_m="
                  << spatial.distance_m << '\n';

        MahalanobisGateOptions covarianceOptions{};
        covarianceOptions.max_elapsed=Duration{100};
        const auto covariance=DiagonalMahalanobisGate{covarianceOptions}.evaluate(
            {at(10),{0.0,0.0,0.0},{0.5,0.5,0.5}},
            {at(20),{0.3,0.0,0.0},{0.5,0.5,0.5}});
        if(!covariance.compatible)
            throw std::runtime_error("covariance gating");
        std::cout << "mahalanobis_gate=accepted squared_distance="
                  << covariance.squared_distance << '\n';

        BatchAssignmentOptions batchOptions{};
        batchOptions.unmatched_penalty=2.0;
        HungarianEventMatcher matcher{batchOptions};
        const std::vector<std::vector<std::optional<double>>> costs{
            {0.2,5.0},
            {4.0,0.1},
            {std::nullopt,std::nullopt}};
        const auto assignment=matcher.match(costs,2);
        if(assignment.track_for_observation.size()!=3 ||
           assignment.track_for_observation[0]!=std::optional<std::size_t>{0} ||
           assignment.track_for_observation[1]!=std::optional<std::size_t>{1} ||
           assignment.track_for_observation[2].has_value() ||
           !assignment.unmatched_tracks.empty())
            throw std::runtime_error("Hungarian batch assignment");
        std::cout << "assignment=0->0,1->1,2->unmatched cost="
                  << assignment.total_cost << '\n';
        // Parameterized executable examples complement the fixed smoke above.
        // Each case asserts the public one-to-one, gating and penalty contract.
        if (scenario == "crossed") {
            const std::vector<std::vector<std::optional<double>>> crossed{
                {4.0, 0.2}, {0.1, 4.0}};
            const auto result = matcher.match(crossed, 2);
            if (result.track_for_observation !=
                    std::vector<std::optional<std::size_t>>{1, 0} ||
                !result.unmatched_tracks.empty() ||
                std::abs(result.total_cost - 0.3) > 1e-9)
                throw std::runtime_error("crossed assignment");
            std::cout << "scenario=crossed assignment=0->1,1->0\n";
        } else if (scenario == "gated") {
            const std::vector<std::vector<std::optional<double>>> gated{
                {std::nullopt, 0.1}, {std::nullopt, std::nullopt}};
            const auto result = matcher.match(gated, 2);
            if (result.track_for_observation !=
                    std::vector<std::optional<std::size_t>>{1, std::nullopt} ||
                result.unmatched_tracks != std::vector<std::size_t>{0} ||
                std::abs(result.total_cost - 2.1) > 1e-9)
                throw std::runtime_error("forbidden-edge assignment");
            std::cout << "scenario=gated assignment=0->1,1->unmatched\n";
        } else if (scenario == "penalty") {
            BatchAssignmentOptions parameterized{};
            parameterized.unmatched_penalty = penalty;
            const std::vector<std::vector<std::optional<double>>> costs{{0.4}};
            const auto result = HungarianEventMatcher{parameterized}.match(costs, 1);
            const bool matched = result.track_for_observation ==
                std::vector<std::optional<std::size_t>>{0};
            const bool unmatched = result.track_for_observation ==
                std::vector<std::optional<std::size_t>>{std::nullopt};
            if ((!matched && !unmatched) ||
                (penalty < 0.4 && !unmatched) ||
                (penalty > 0.4 && !matched) ||
                std::abs(result.total_cost - std::min(penalty, 0.4)) > 1e-9)
                throw std::runtime_error("penalty tradeoff");
            std::cout << "scenario=penalty assignment=0->"
                      << (matched ? "0" : "unmatched")
                      << " cost=" << result.total_cost << '\n';
        }
        std::cout << "temporal-association=ok\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "temporal-association=error reason=" << error.what() << '\n';
        return 1;
    }
}

#include <audition/temporal/hungarian_event_matcher.hpp>
#include <audition/temporal/interval_relations.hpp>
#include <audition/temporal/mahalanobis_gate.hpp>
#include <audition/temporal/spatial_temporal_gate.hpp>

#include <audition/core/time.hpp>

#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

int main() {
    using namespace audition;
    using namespace audition::temporal;
    try {
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
        std::cout << "temporal-association=ok\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "temporal-association=error reason=" << error.what() << '\n';
        return 1;
    }
}

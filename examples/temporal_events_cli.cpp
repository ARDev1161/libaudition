#include <audition/audition.hpp>
#include <audition/temporal/event_episode_associator.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

// All inputs are deterministic synthetic evidence. The example exercises
// library state transitions, not classifier accuracy or acoustic inference.
int main() {
    using namespace audition;
    using namespace audition::temporal;
    const auto at=[](std::int64_t ms) {
        return Timestamp{ms*1000000LL,{ClockDomain::Monotonic,0U}};
    };
    try {
        TemporalScalarOptions scalarOptions{};
        scalarOptions.alpha=1.0;
        scalarOptions.drift=0.0;
        scalarOptions.threshold=1.0;
        TemporalScalarAnalyzer changes{scalarOptions};
        int rises=0;
        for(std::size_t i=0;i<5;++i) {
            const double values[]{0,0,3,3,3};
            const auto evidence=changes.observe(
                {AcousticSourceId{7},"activity",at(static_cast<std::int64_t>(i)),values[i]});
            if(evidence.positive_change) ++rises;
        }
        if(rises!=1) throw std::runtime_error("scalar change detection");
        std::cout << "scalar_positive_changes=" << rises << '\n';

        TemporalAcousticEventDetector detector{};
        EventEpisodeAssociator episodes{};
        int starts=0,ends=0;
        std::uint64_t occurrence=0;
        std::uint64_t episode=0;
        const double scores[]{0.8,0.9,0.1,0.1};
        for(std::size_t i=0;i<4;++i) {
            ClassificationResult observation{};
            observation.classes.push_back({"alarm",Probability::from(scores[i])});
            const auto transitions=detector.observe(
                AcousticSourceId{7},at(static_cast<std::int64_t>(i+10)),observation);
            for(const auto& transition:transitions) {
                const bool active=transition.kind==TemporalAcousticEventKind::Started;
                if(active) {
                    occurrence=transition.occurrence_id;
                    ++starts;
                } else {
                    if(transition.occurrence_id!=occurrence)
                        throw std::runtime_error("occurrence lifecycle changed");
                    ++ends;
                }
                const auto associated=episodes.observe(
                    {transition.source_id,transition.label,
                     at(static_cast<std::int64_t>(i+10)),active});
                if(associated.size()!=1)
                    throw std::runtime_error("unexpected episode association");
                if(active) episode=associated.front().episode_id;
                else if(associated.front().episode_id!=episode)
                    throw std::runtime_error("episode identity changed");
            }
        }
        if(starts!=1 || ends!=1 || !detector.activeEvents().empty() ||
           occurrence==0 || episode==0)
            throw std::runtime_error("incomplete temporal event lifecycle");
        std::cout << "event_starts=" << starts << " event_ends=" << ends << '\n';
        std::cout << "association_episode=" << episode << '\n';
        std::cout << "temporal-events=ok\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "temporal-events=error reason=" << e.what() << '\n';
        return 1;
    }
}

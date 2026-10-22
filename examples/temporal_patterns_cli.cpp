#include <audition/audition.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace audition;
    using namespace audition::temporal;

    if (argc==2 && std::string{argv[1]}=="--help") {
        std::cout << "Usage: libaudition_temporal_cli [scalar_features...]\n"
                     "At least 6 finite numbers; defaults to 1 2 3 repeated 3x.\n"
                     "Compares 3-frame windows, discovers motifs and detects a "
                     "per-source streaming pattern.\n";
        return 0;
    }

    try {
        const std::vector<double> defaults{1,2,3,1,2,3,1,2,3};
        std::vector<double> scalars;
        if (argc==1) {
            scalars=defaults;
        } else {
            for(int i=1;i<argc;++i) {
                const auto str=std::string{argv[i]};
                std::size_t pos=0;
                const double value=std::stod(str,&pos);
                if (pos!=str.size() || !std::isfinite(value))
                    throw std::invalid_argument("nonfinite or invalid feature");
                scalars.push_back(value);
            }
        }
        if (scalars.size()<6 || scalars.size()>64)
            throw std::invalid_argument("expected 6..64 scalar feature frames");

        std::vector<std::vector<double>> frames;
        frames.reserve(scalars.size());
        for(double scalar:scalars) frames.push_back({scalar});

        RecurrenceAnalyzer recurrence;
        RecurrenceEstimate periodic{};
        for(auto ms : {0LL,100LL,200LL,300LL})
            periodic=recurrence.observe(AcousticSourceId{1},"pulse",
                Timestamp{ms*1000000LL,{ClockDomain::Monotonic,0U}});
        if(!periodic.periodic || periodic.median_period.nanoseconds()!=100000000LL)
            throw std::runtime_error("unexpected recurrence period");
        std::cout << "recurrence_period_ms="
                  << periodic.median_period.nanoseconds()/1000000LL << '\n';

        std::vector<std::vector<double>> first(frames.begin(),frames.begin()+3);
        std::vector<std::vector<double>> second(frames.begin()+3,frames.begin()+6);
        DynamicTimeWarping dtw;
        const auto alignment=dtw.compare(first,second);
        if(!alignment.reachable || !std::isfinite(alignment.normalized_cost))
            throw std::runtime_error("DTW alignment unreachable");
        std::cout << "dtw_normalized_cost=" << alignment.normalized_cost << '\n';

        MotifOptions opts{};
        opts.window_frames=3;
        opts.max_normalized_cost=0.001;
        const auto motif=AcousticMotifFinder{opts}.find(frames);
        std::cout << "motif=" << (motif.found ? "found" : "none")
                  << " first=" << motif.first_start
                  << " second=" << motif.second_start << '\n';

        StreamingPatternOptions stream_opts{};
        stream_opts.max_normalized_cost=0.001;
        stream_opts.cooldown_frames=2;
        StreamingPatternMatcher matcher{stream_opts};
        matcher.registerTemplate("pulse-shape",first);
        std::size_t matches=0;
        for(const auto& frame:frames)
            matches+=matcher.push(AcousticSourceId{1},frame).size();
        std::cout << "stream_matches=" << matches << '\n';
        if(argc==1 && (!motif.found || matches!=3 ||
                       alignment.normalized_cost!=0))
            throw std::runtime_error("default temporal example regression");
        std::cout << "temporal=ok\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "temporal=error reason=" << error.what() << '\n';
        return 1;
    }
}

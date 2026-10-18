#pragma once

#include <audition/temporal/dynamic_time_warping.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace audition::temporal {

// Offline fixed-window motif discovery in an already extracted feature
// sequence. Non-overlapping candidates avoid trivial self-matches.
// This API does not infer semantic sound-event labels or source identity.
struct MotifOptions {
    std::size_t max_frames{256};
    std::size_t max_dimensions{64};
    std::size_t window_frames{8};
    std::size_t stride{1};
    std::size_t warping_radius{2};
    double max_normalized_cost{0.1};
};
struct MotifMatch {
    std::size_t first_start{0};
    std::size_t second_start{0};
    double normalized_cost{0};
    bool found{false};
};

class AcousticMotifFinder final {
public:
    explicit AcousticMotifFinder(MotifOptions options={}) : options_(options),
        dtw_({options.max_frames,options.max_dimensions,options.warping_radius}) {
        if (!options.max_frames || !options.max_dimensions || !options.window_frames ||
            options.window_frames>options.max_frames || !options.stride ||
            !std::isfinite(options.max_normalized_cost) || options.max_normalized_cost<0)
            throw std::invalid_argument("invalid motif options");
    }

    [[nodiscard]] MotifMatch find(const std::vector<std::vector<double>>& frames) const {
        if (frames.size()>options_.max_frames)
            throw std::length_error("motif frame capacity exceeded");
        if (frames.empty()) return {};
        const auto dimensions=frames.front().size();
        if (!dimensions || dimensions>options_.max_dimensions)
            throw std::invalid_argument("invalid motif feature dimension");
        for(const auto& frame:frames) {
            if(frame.size()!=dimensions)
                throw std::invalid_argument("inconsistent motif feature dimension");
            for(const double x:frame)
                if(!std::isfinite(x)) throw std::invalid_argument("nonfinite motif feature");
        }
        if(frames.size()<2*options_.window_frames) return {};
        MotifMatch best{};
        double min_cost=std::numeric_limits<double>::infinity();
        for(std::size_t a=0;a+options_.window_frames<=frames.size();a+=options_.stride) {
            const std::vector<std::vector<double>> left(
                frames.begin()+static_cast<std::ptrdiff_t>(a),
                frames.begin()+static_cast<std::ptrdiff_t>(a+options_.window_frames));
            for(std::size_t b=a+options_.window_frames;
                b+options_.window_frames<=frames.size();b+=options_.stride) {
                const std::vector<std::vector<double>> right(
                    frames.begin()+static_cast<std::ptrdiff_t>(b),
                    frames.begin()+static_cast<std::ptrdiff_t>(b+options_.window_frames));
                const auto result=dtw_.compare(left,right);
                if(result.reachable && result.normalized_cost<min_cost) {
                    min_cost=result.normalized_cost;
                    best={a,b,result.normalized_cost,
                          result.normalized_cost<=options_.max_normalized_cost};
                }
            }
        }
        if(!best.found) return {};
        return best;
    }
private:
    MotifOptions options_;
    DynamicTimeWarping dtw_;
};

} // namespace audition::temporal

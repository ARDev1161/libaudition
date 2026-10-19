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

    // Select up to max_matches disjoint motif pairs, in ascending DTW cost.
    // Each selected window is exclusive: neither it nor any overlapping
    // candidate window may participate in a later selected match.
    [[nodiscard]] std::vector<MotifMatch> findMultiple(
        const std::vector<std::vector<double>>& frames,
        std::size_t max_matches) const {
        if (!max_matches) throw std::invalid_argument("motif result capacity must be positive");
        // A single result is exactly the best pair returned by find().
        // Avoid retaining/sorting O(candidate_pairs) matches for this common
        // bounded-memory use case; find() keeps only its best pair.
        if (max_matches==1) {
            const auto best=find(frames);
            return best.found ? std::vector<MotifMatch>{best} : std::vector<MotifMatch>{};
        }
        if (frames.size()>options_.max_frames)
            throw std::length_error("motif frame capacity exceeded");
        if (frames.empty()) return {};
        const auto dimensions=frames.front().size();
        if (!dimensions || dimensions>options_.max_dimensions)
            throw std::invalid_argument("invalid motif feature dimension");
        for (const auto& frame:frames) {
            if (frame.size()!=dimensions)
                throw std::invalid_argument("inconsistent motif feature dimension");
            for (double value:frame)
                if (!std::isfinite(value))
                    throw std::invalid_argument("nonfinite motif feature");
        }
        if (frames.size()/options_.window_frames<2) return {};
        std::vector<MotifMatch> candidates;
        DtwWorkspace workspace; // Invocation-local scratch for concurrent const calls.
        // DTW reads windows directly from the validated input. It does
        // not copy feature frames into temporary left/right sequences.
        const auto last_start=frames.size()-options_.window_frames;
        for (std::size_t a=0;a<=last_start;) {
            for (std::size_t b=a+options_.window_frames;b<=last_start;) {
                const auto aligned=dtw_.compareValidatedWindows(
                    frames,a,b,options_.window_frames,workspace);
                if (aligned.reachable &&
                    aligned.normalized_cost<=options_.max_normalized_cost)
                    candidates.push_back({a,b,aligned.normalized_cost,true});
                if (options_.stride>last_start-b) break;
                b+=options_.stride;
            }
            if (options_.stride>last_start-a) break;
            a+=options_.stride;
        }
        std::sort(candidates.begin(),candidates.end(),[](const auto& x,const auto& y) {
            if (x.normalized_cost!=y.normalized_cost)
                return x.normalized_cost<y.normalized_cost;
            if (x.first_start!=y.first_start) return x.first_start<y.first_start;
            return x.second_start<y.second_start;
        });
        std::vector<MotifMatch> selected;
        const auto overlaps=[this](std::size_t a,std::size_t b) {
            return a<b ? b-a<options_.window_frames : a-b<options_.window_frames;
        };
        for (const auto& candidate:candidates) {
            bool conflict=false;
            for (const auto& match:selected)
                if (overlaps(candidate.first_start,match.first_start) ||
                    overlaps(candidate.first_start,match.second_start) ||
                    overlaps(candidate.second_start,match.first_start) ||
                    overlaps(candidate.second_start,match.second_start)) {
                    conflict=true;
                    break;
                }
            if (!conflict) {
                selected.push_back(candidate);
                if (selected.size()==max_matches) break;
            }
        }
        return selected;
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
        // Division avoids overflow in 2 * window_frames.
        if (frames.size()/options_.window_frames<2) return {};
        const auto last_start=frames.size()-options_.window_frames;
        MotifMatch best{};
        DtwWorkspace workspace; // Invocation-local scratch; no shared mutable state.
        double min_cost=std::numeric_limits<double>::infinity();
        for (std::size_t a=0;a<=last_start;) {
            for (std::size_t b=a+options_.window_frames;b<=last_start;) {
                const auto result=dtw_.compareValidatedWindows(
                    frames,a,b,options_.window_frames,workspace);
                if (result.reachable && result.normalized_cost<min_cost) {
                    min_cost=result.normalized_cost;
                    best={a,b,result.normalized_cost,
                          result.normalized_cost<=options_.max_normalized_cost};
                }
                if (options_.stride>last_start-b) break;
                b+=options_.stride;
            }
            if (options_.stride>last_start-a) break;
            a+=options_.stride;
        }
        if(!best.found) return {};
        return best;
    }
private:
    MotifOptions options_;
    DynamicTimeWarping dtw_;
};

} // namespace audition::temporal

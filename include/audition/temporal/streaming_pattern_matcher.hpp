#pragma once

#include <audition/temporal/dynamic_time_warping.hpp>
#include <audition/core/id.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace audition::temporal {

// Recognize a registered feature template at the end of a bounded stream.
// One detector per persistent source; each push tests the most recent window.
// Matching is observational: it does not assign physical or semantic identity.
struct StreamingPatternOptions {
    std::size_t max_sources{16};
    std::size_t max_templates{16};
    std::size_t max_frames{64};
    std::size_t max_dimensions{64};
    std::size_t warping_radius{4};
    double max_normalized_cost{0.1};
    std::size_t min_candidate_frames{0}; // 0 = exact template length
    std::size_t max_candidate_frames{0}; // 0 = exact template length
    std::size_t cooldown_frames{0}; // 0 preserves emission on every matching frame
};
struct StreamingPatternMatch {
    std::string label{};
    double normalized_cost{0};
    std::size_t ending_frame{0};
};

class StreamingPatternMatcher final {
public:
    explicit StreamingPatternMatcher(StreamingPatternOptions options={})
        : options_(options),dtw_({options.max_frames,options.max_dimensions,options.warping_radius}) {
        if (!options.max_sources || !options.max_templates || !options.max_frames ||
            !options.max_dimensions ||
            (options.min_candidate_frames && options.min_candidate_frames>options.max_frames) ||
            (options.max_candidate_frames && options.max_candidate_frames>options.max_frames) ||
            (options.min_candidate_frames && options.max_candidate_frames &&
             options.min_candidate_frames>options.max_candidate_frames) ||
            !std::isfinite(options.max_normalized_cost) ||
            options.max_normalized_cost<0)
            throw std::invalid_argument("invalid streaming pattern options");
    }

    void registerTemplate(std::string label,std::vector<std::vector<double>> frames) {
        if (label.empty() || frames.empty() || frames.size()>options_.max_frames)
            throw std::invalid_argument("invalid pattern template");
        if (!templates_.count(label) && templates_.size()>=options_.max_templates)
            throw std::length_error("pattern template capacity");
        const auto dimensions=frames.front().size();
        if (!dimensions || dimensions>options_.max_dimensions)
            throw std::invalid_argument("invalid pattern template dimensions");
        for(const auto& frame:frames) {
            if (frame.size()!=dimensions) throw std::invalid_argument("ragged template");
            for(double value:frame)
                if (!std::isfinite(value)) throw std::invalid_argument("nonfinite template");
        }
        templates_[std::move(label)]=std::move(frames);
    }

    [[nodiscard]] std::vector<StreamingPatternMatch> push(AcousticSourceId source,
                                                           const std::vector<double>& frame) {
        if (!source.valid() || frame.empty() || frame.size()>options_.max_dimensions)
            throw std::invalid_argument("invalid streaming pattern frame");
        for(double value:frame)
            if (!std::isfinite(value)) throw std::invalid_argument("nonfinite frame");
        auto it=states_.find(source);
        if (it==states_.end() && states_.size()>=options_.max_sources)
            throw std::length_error("streaming source capacity");
        if (it!=states_.end() && !it->second.history.empty() &&
            it->second.history.back().size()!=frame.size())
            throw std::invalid_argument("streaming frame dimension changed");
        State next=it==states_.end()?State{}:it->second;
        next.history.push_back(frame);
        if(next.history.size()>options_.max_frames) next.history.pop_front();
        if (next.frames_seen==std::numeric_limits<std::size_t>::max())
            throw std::overflow_error("streaming pattern frame counter overflow");
        ++next.frames_seen;
        std::vector<StreamingPatternMatch> matches;
        // Candidate suffixes depend on the observation length, not on the
        // template label. Build each length at most once per accepted frame.
        // Preserve the local next state until every DTW comparison succeeds.
        std::map<std::size_t,std::vector<std::vector<double>>> suffix_cache;
        for(const auto& entry:templates_) {
            const auto& reference=entry.second;
            if (reference.front().size()!=frame.size()) continue;
            const auto min_length=options_.min_candidate_frames ?
                options_.min_candidate_frames : reference.size();
            const auto max_length=options_.max_candidate_frames ?
                options_.max_candidate_frames : reference.size();
            if (min_length>max_length) continue;
            double best_cost=std::numeric_limits<double>::infinity();
            for (std::size_t length=min_length;
                 length<=max_length && length<=next.history.size();++length) {
                auto cached=suffix_cache.find(length);
                if (cached==suffix_cache.end()) {
                    std::vector<std::vector<double>> suffix(
                        next.history.end()-static_cast<std::ptrdiff_t>(length),
                        next.history.end());
                    cached=suffix_cache.emplace(length,std::move(suffix)).first;
                }
                const auto match=dtw_.compare(reference,cached->second);
                if (match.reachable && match.normalized_cost<best_cost)
                    best_cost=match.normalized_cost;
            }
            if (best_cost<=options_.max_normalized_cost) {
                const auto last=next.last_emission.find(entry.first);
                const bool suppressed=options_.cooldown_frames>0 &&
                    last!=next.last_emission.end() &&
                    next.frames_seen-last->second<=options_.cooldown_frames;
                if (!suppressed) {
                    matches.push_back({entry.first,best_cost,next.frames_seen});
                    next.last_emission[entry.first]=next.frames_seen;
                }
            }
        }
        states_[source]=std::move(next);
        return matches;
    }

    void forget(AcousticSourceId source) noexcept {states_.erase(source);}
    void reset() noexcept {states_.clear();}
private:
    struct State {
        std::deque<std::vector<double>> history{};
        std::size_t frames_seen{0};
        std::map<std::string,std::size_t> last_emission{};
    };
    StreamingPatternOptions options_;
    DynamicTimeWarping dtw_;
    std::map<std::string,std::vector<std::vector<double>>> templates_;
    std::map<AcousticSourceId,State> states_;
};
} // namespace audition::temporal

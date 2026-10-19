#pragma once

#include <audition/temporal/dynamic_time_warping.hpp>
#include <audition/core/id.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <iterator>
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
        // Replacing a label changes the meaning of its matches. Reset only
        // that label's suppression state after the validated replacement is
        // committed; keep all feature histories and other labels unchanged.
        const auto inserted=templates_.insert_or_assign(std::move(label),std::move(frames));
        if (!inserted.second) {
            for (auto& state:states_)
                state.second.last_emission.erase(inserted.first->first);
        }
    }

    // Removes a template and any cooldown state bound to its label while
    // preserving source histories and other registered templates.
    // Returns false for an unknown label (no mutation).
    bool unregisterTemplate(const std::string& label) {
        const auto it=templates_.find(label);
        if (it==templates_.end()) return false;
        for (auto& state:states_)
            state.second.last_emission.erase(label);
        templates_.erase(it);
        return true;
    }

    // Convenience API: returning a nonempty vector may allocate.
    [[nodiscard]] std::vector<StreamingPatternMatch> push(AcousticSourceId source,
                                                           const std::vector<double>& frame) {
        std::vector<StreamingPatternMatch> matches;
        pushInto(source,frame,matches);
        return matches;
    }

    // Allocation-conscious API: callers may reserve max_templates elements in
    // output and reuse it across pushes. Output is cleared on a valid push;
    // on a comparison/allocation exception it may contain partial results,
    // but source history and cooldown remain unmodified.
    // Like push(), this API is not thread-safe on the same matcher.
    void pushInto(AcousticSourceId source,const std::vector<double>& frame,
                  std::vector<StreamingPatternMatch>& output) {
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
        // Evaluate against a virtual history ending in the incoming frame.
        // No candidate feature copies; source history remains read-only
        // until all comparisons and output assembly succeed.
        const State* prior=it==states_.end()?nullptr:&it->second;
        const std::size_t seen=prior?prior->frames_seen:0;
        if (seen==std::numeric_limits<std::size_t>::max())
            throw std::overflow_error("streaming pattern frame counter overflow");
        const std::size_t ending_frame=seen+1;
        const std::size_t retained=prior?prior->history.size():0;
        const std::size_t available=std::min(options_.max_frames,retained+1);
        output.clear();
        auto& matches=output;
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
                 length<=max_length && length<=available;++length) {
                // Directly read the trailing source history and incoming
                // frame: no suffix feature copies or cache retained.
                const auto match=dtw_.compareValidatedSuffix(
                    reference,prior ? &prior->history : nullptr,frame,length,workspace_);
                if (match.reachable && match.normalized_cost<best_cost)
                    best_cost=match.normalized_cost;
            }
            if (best_cost<=options_.max_normalized_cost) {
                bool suppressed=false;
                if (options_.cooldown_frames>0 && prior) {
                    const auto last=prior->last_emission.find(entry.first);
                    suppressed=last!=prior->last_emission.end() &&
                        ending_frame-last->second<=options_.cooldown_frames;
                }
                if (!suppressed)
                    matches.push_back({entry.first,best_cost,ending_frame});
            }
        }
        // No cooldown means emission is stateless: avoid cloning or updating
        // the cooldown map on every match. When enabled, stage all potentially
        // throwing map updates before committing accepted source history.
        std::map<std::string,std::size_t> emission;
        const bool record_cooldown=options_.cooldown_frames>0 && !matches.empty();
        if (record_cooldown) {
            if (prior) emission=prior->last_emission;
            for (const auto& match:matches)
                emission[match.label]=ending_frame;
        }
        if (!prior) {
            State next{};
            next.history.push_back(frame);
            next.frames_seen=ending_frame;
            next.last_emission=std::move(emission);
            states_.emplace(source,std::move(next));
        } else {
            // Copy the incoming frame before touching the deque. No DTW
            // computation or validation may fail after this point.
            // Keep a reusable staging vector. A failed copy leaves the
            // committed history intact; all subsequent full-ring operations
            // are noexcept vector swaps.
            auto& state=it->second;
            state.pending_frame.assign(frame.begin(),frame.end());
            if (state.history.size()==options_.max_frames) {
                state.history.front().swap(state.pending_frame);
                std::rotate(state.history.begin(),
                            std::next(state.history.begin()),state.history.end());
            } else {
                state.history.push_back(std::move(state.pending_frame));
            }
            state.frames_seen=ending_frame;
            if (record_cooldown) state.last_emission.swap(emission);
        }
    }

    void forget(AcousticSourceId source) noexcept {states_.erase(source);}
    void reset() noexcept {states_.clear();}
private:
    struct State {
        std::deque<std::vector<double>> history{};
        std::vector<double> pending_frame{};
        std::size_t frames_seen{0};
        std::map<std::string,std::size_t> last_emission{};
    };
    StreamingPatternOptions options_;
    DynamicTimeWarping dtw_;
    // Scratch reused across pushes; like source state, this matcher is not thread-safe.
    DtwWorkspace workspace_;
    std::map<std::string,std::vector<std::vector<double>>> templates_;
    std::map<AcousticSourceId,State> states_;
};
} // namespace audition::temporal

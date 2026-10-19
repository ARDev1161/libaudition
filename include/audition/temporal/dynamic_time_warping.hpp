#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>
#include <stdexcept>
#include <vector>

namespace audition::temporal {

// Bounded dynamic time warping for sequences of fixed-size feature vectors.
// Squared Euclidean local cost and three admissible moves (diagonal/left/up).
// Reported cost is cumulative, not a calibrated similarity probability.
struct DtwOptions {
    std::size_t max_frames{256};
    std::size_t max_dimensions{64};
    std::size_t warping_radius{64}; // |index_a-index_b| <= radius
};
struct DtwResult {
    double total_cost{0};
    double normalized_cost{0}; // total_cost / length of selected warping path
    std::size_t path_length{0};
    bool reachable{false};
};

// Caller-owned scratch buffers; reuse across comparisons to avoid repeated
// DP-row allocations. Do not share a workspace across concurrent calls.
struct DtwWorkspace {
    struct Cell { double cost; std::size_t steps; };
    std::vector<Cell> previous{};
    std::vector<Cell> current{};
};

class DynamicTimeWarping final {
public:
    explicit DynamicTimeWarping(DtwOptions options = {}) : options_(options) {
        if (!options_.max_frames || !options_.max_dimensions)
            throw std::invalid_argument("DTW capacity must be positive");
    }

    [[nodiscard]] DtwResult compare(
        const std::vector<std::vector<double>>& a,
        const std::vector<std::vector<double>>& b) const {
        DtwWorkspace workspace;
        return compare(a,b,workspace);
    }

    [[nodiscard]] DtwResult compare(
        const std::vector<std::vector<double>>& a,
        const std::vector<std::vector<double>>& b,
        DtwWorkspace& workspace) const {
        return compareRanges(a,0,a.size(),b,0,b.size(),workspace);
    }

    // Compare two fixed-length windows in one feature sequence without
    // allocating/copying window vectors. Windows may overlap; this API does
    // not itself impose a motif-search non-overlap policy. The caller owns
    // scratch and must not use it concurrently.
    [[nodiscard]] DtwResult compareWindows(
        const std::vector<std::vector<double>>& frames,
        std::size_t first_start,
        std::size_t second_start,
        std::size_t window_frames,
        DtwWorkspace& workspace) const {
        return compareRanges(frames,first_start,window_frames,
                             frames,second_start,window_frames,workspace);
    }

private:
    // The motif finder validates its entire immutable input once before the
    // candidate loop. Rechecking each frame at every pair is quadratic
    // redundant work. Keep this overload private: unvalidated callers must
    // use compareWindows(), whose validation contract is unchanged.
    friend class AcousticMotifFinder;
    friend class StreamingPatternMatcher;

    // Streaming-only validated view: template, retained history and incoming
    // frame have already passed the matcher's dimension/capacity checks.
    // No feature vectors or pointer arrays are materialized.
    [[nodiscard]] DtwResult compareValidatedSuffix(
        const std::vector<std::vector<double>>& reference,
        const std::deque<std::vector<double>>* history,
        const std::vector<double>& incoming,
        std::size_t candidate_frames,
        DtwWorkspace& workspace) const {
        const std::size_t retained=history ? history->size() : 0;
        if (reference.empty() || !candidate_frames ||
            candidate_frames-1>retained)
            throw std::invalid_argument("invalid DTW streaming suffix");
        if (reference.size()>options_.max_frames ||
            candidate_frames>options_.max_frames)
            throw std::length_error("DTW sequence exceeds frame capacity");
        const std::size_t old_frames=candidate_frames-1;
        const std::size_t first_old=retained-old_frames;
        return compareIndexed(reference.size(),candidate_frames,
            reference.front().size(),
            [&](std::size_t i) -> const std::vector<double>& {
                return reference[i];
            },
            [&](std::size_t j) -> const std::vector<double>& {
                // A null history is valid only on the first (incoming) frame.
                return j==old_frames ? incoming : (*history)[first_old+j];
            },workspace);
    }


    [[nodiscard]] DtwResult compareValidatedWindows(
        const std::vector<std::vector<double>>& frames,
        std::size_t first_start,
        std::size_t second_start,
        std::size_t window_frames,
        DtwWorkspace& workspace) const {
        return compareRanges(frames,first_start,window_frames,
                             frames,second_start,window_frames,workspace,false);
    }

    [[nodiscard]] DtwResult compareRanges(
        const std::vector<std::vector<double>>& a,
        std::size_t first_start,
        std::size_t first_size,
        const std::vector<std::vector<double>>& b,
        std::size_t second_start,
        std::size_t second_size,
        DtwWorkspace& workspace,
        bool validate_features=true) const {
        if (!first_size || !second_size)
            throw std::invalid_argument("DTW sequences must be nonempty");
        if (first_size>options_.max_frames || second_size>options_.max_frames)
            throw std::length_error("DTW sequence exceeds frame capacity");
        // Subtraction after the offset check avoids size_t range overflow.
        if (first_start>a.size() || first_size>a.size()-first_start ||
            second_start>b.size() || second_size>b.size()-second_start)
            throw std::out_of_range("DTW window outside input sequence");
        const auto dimensions=a[first_start].size();
        if (!dimensions || dimensions>options_.max_dimensions)
            throw std::invalid_argument("invalid DTW feature dimensions");
        const auto validate=[dimensions](
            const std::vector<std::vector<double>>& sequence,
            std::size_t start,std::size_t count) {
            for (std::size_t i=0;i<count;++i) {
                const auto& feature=sequence[start+i];
                if (feature.size()!=dimensions)
                    throw std::invalid_argument("inconsistent DTW feature dimensions");
                for (double component:feature)
                    if (!std::isfinite(component))
                        throw std::invalid_argument("nonfinite DTW feature");
            }
        };
        if (validate_features) {
            validate(a,first_start,first_size);
            validate(b,second_start,second_size);
        }
        return compareIndexed(first_size,second_size,dimensions,
            [&](std::size_t i) -> const std::vector<double>& {
                return a[first_start+i];
            },
            [&](std::size_t j) -> const std::vector<double>& {
                return b[second_start+j];
            },workspace);
    }

    template<class FirstFrame,class SecondFrame>
    [[nodiscard]] DtwResult compareIndexed(
        std::size_t first_size,std::size_t second_size,
        std::size_t dimensions,FirstFrame&& first,SecondFrame&& second,
        DtwWorkspace& workspace) const {
        const double inf=std::numeric_limits<double>::infinity();
        using Cell=DtwWorkspace::Cell;
        auto& previous=workspace.previous;
        auto& current=workspace.current;
        previous.resize(second_size+1);
        current.resize(second_size+1);
        std::fill(previous.begin(),previous.end(),Cell{inf,0});
        previous[0]={0,0};
        for (std::size_t i=1;i<=first_size;++i) {
            std::fill(current.begin(),current.end(),Cell{inf,0});
            const std::size_t low=i>options_.warping_radius ?
                i-options_.warping_radius : 1;
            // Compute the high edge without adding a potentially huge radius
            // to the row index, which could wrap unsigned arithmetic.
            const std::size_t high=i>=second_size ? second_size :
                i+std::min(options_.warping_radius,second_size-i);
            for (std::size_t j=low;j<=high;++j) {
                double local=0;
                for (std::size_t k=0;k<dimensions;++k) {
                    const double delta=first(i-1)[k]-second(j-1)[k];
                    local+=delta*delta;
                }
                if (!std::isfinite(local))
                    throw std::overflow_error("DTW local cost overflow");
                Cell best=previous[j-1];
                if (previous[j].cost<best.cost) best=previous[j];
                if (current[j-1].cost<best.cost) best=current[j-1];
                if (std::isfinite(best.cost)) {
                    const double cost=best.cost+local;
                    if (!std::isfinite(cost))
                        throw std::overflow_error("DTW accumulated cost overflow");
                    current[j]={cost,best.steps+1};
                }
            }
            previous.swap(current);
        }
        const Cell final=previous[second_size];
        if (!std::isfinite(final.cost)) return {};
        return {final.cost,final.cost/static_cast<double>(final.steps),final.steps,true};
    }

    DtwOptions options_{};
};

} // namespace audition::temporal

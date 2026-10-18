#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
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

class DynamicTimeWarping final {
public:
    explicit DynamicTimeWarping(DtwOptions options = {}) : options_(options) {
        if (!options_.max_frames || !options_.max_dimensions)
            throw std::invalid_argument("DTW capacity must be positive");
    }

    [[nodiscard]] DtwResult compare(
        const std::vector<std::vector<double>>& a,
        const std::vector<std::vector<double>>& b) const {
        if (a.empty() || b.empty())
            throw std::invalid_argument("DTW sequences must be nonempty");
        if (a.size() > options_.max_frames || b.size() > options_.max_frames)
            throw std::length_error("DTW sequence exceeds frame capacity");
        const auto dimensions = a.front().size();
        if (!dimensions || dimensions > options_.max_dimensions)
            throw std::invalid_argument("invalid DTW feature dimensions");
        const auto validate = [&](const std::vector<std::vector<double>>& sequence) {
            for (const auto& frame:sequence) {
                if (frame.size() != dimensions)
                    throw std::invalid_argument("inconsistent DTW feature dimensions");
                for (double component:frame)
                    if (!std::isfinite(component))
                        throw std::invalid_argument("nonfinite DTW feature");
            }
        };
        validate(a);
        validate(b);
        const double inf=std::numeric_limits<double>::infinity();
        struct Cell {double cost;std::size_t steps;};
        std::vector<Cell> previous(b.size()+1,{inf,0});
        std::vector<Cell> current(b.size()+1,{inf,0});
        previous[0]={0,0};
        for (std::size_t i=1;i<=a.size();++i) {
            std::fill(current.begin(),current.end(),Cell{inf,0});
            const std::size_t low=i>options_.warping_radius ? i-options_.warping_radius : 1;
            const std::size_t high=std::min(b.size(),i+std::min(options_.warping_radius,b.size()));
            for (std::size_t j=low;j<=high;++j) {
                double local=0;
                for (std::size_t k=0;k<dimensions;++k) {
                    const double delta=a[i-1][k]-b[j-1][k];
                    local+=delta*delta;
                }
                if (!std::isfinite(local)) throw std::overflow_error("DTW local cost overflow");
                Cell best=previous[j-1];
                if (previous[j].cost < best.cost) best=previous[j];
                if (current[j-1].cost < best.cost) best=current[j-1];
                if (std::isfinite(best.cost)) {
                    const double cost=best.cost+local;
                    if (!std::isfinite(cost)) throw std::overflow_error("DTW accumulated cost overflow");
                    current[j]={cost,best.steps+1};
                }
            }
            previous.swap(current);
        }
        const Cell final=previous[b.size()];
        if (!std::isfinite(final.cost)) return {};
        return {final.cost,final.cost/static_cast<double>(final.steps),final.steps,true};
    }
private:
    DtwOptions options_{};
};

} // namespace audition::temporal

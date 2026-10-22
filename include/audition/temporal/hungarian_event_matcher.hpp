#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace audition::temporal {

// Minimum-cost one-to-one assignment of observation rows to existing tracks.
// Each row may remain unmatched for a fixed penalty. nullopt forbids an edge.
// Costs must be finite, nonnegative and in a shared caller-defined unit.
// Matching does not establish physical identity; callers own ID lifecycle.
struct BatchAssignmentOptions {
    double unmatched_penalty{10.0};
    std::size_t max_observations{64};
    std::size_t max_tracks{64};
};
struct BatchAssignment {
    std::vector<std::optional<std::size_t>> track_for_observation{};
    std::vector<std::size_t> unmatched_tracks{};
    double total_cost{0};
};

class HungarianEventMatcher final {
public:
    explicit HungarianEventMatcher(BatchAssignmentOptions o={}) : options_(o) {
        if (!std::isfinite(o.unmatched_penalty) || o.unmatched_penalty<=0 ||
            !o.max_observations || !o.max_tracks)
            throw std::invalid_argument("invalid assignment options");
    }

    [[nodiscard]] BatchAssignment match(
        const std::vector<std::vector<std::optional<double>>>& costs,
        std::size_t track_count) const {
        const std::size_t n=costs.size(),m=track_count;
        if (n>options_.max_observations || m>options_.max_tracks)
            throw std::length_error("assignment capacity exceeded");
        for (const auto& row: costs) {
            if (row.size()!=m) throw std::invalid_argument("ragged assignment matrix");
            for (const auto& v: row)
                if (v && (!std::isfinite(*v) || *v<0))
                    throw std::invalid_argument("invalid assignment cost");
        }
        BatchAssignment result{};
        result.track_for_observation.resize(n);
        if (!n) {
            for(std::size_t j=0;j<m;++j) result.unmatched_tracks.push_back(j);
            return result;
        }
        // n real rows, m+n columns (one unmatched dummy per row).
        // Hungarian shortest augmenting paths. Forbidden edges use infinity:
        // a finite sentinel can be reached through reduced-cost updates and
        // produce suboptimal or invalid assignments.
        const std::size_t cols=m+n;
        const double infinity=std::numeric_limits<double>::infinity();
        std::vector<std::vector<double>> matrix(n+1,std::vector<double>(cols+1,infinity));
        for(std::size_t i=0;i<n;++i) {
            for(std::size_t j=0;j<m;++j)
                if(costs[i][j] && *costs[i][j]<=options_.unmatched_penalty)
                    matrix[i+1][j+1]=*costs[i][j];
            for(std::size_t j=m;j<cols;++j) matrix[i+1][j+1]=options_.unmatched_penalty;
        }
        std::vector<double> u(n+1),v(cols+1);
        std::vector<std::size_t> p(cols+1),way(cols+1);
        for(std::size_t i=1;i<=n;++i) {
            p[0]=i;
            std::size_t j0=0;
            std::vector<double> minv(cols+1,infinity);
            std::vector<bool> used(cols+1);
            do {
                used[j0]=true;
                const auto i0=p[j0];
                double delta=infinity;
                std::size_t j1=0;
                for(std::size_t j=1;j<=cols;++j) if(!used[j]) {
                    if (std::isfinite(matrix[i0][j])) {
                        const double cur=matrix[i0][j]-u[i0]-v[j];
                        if (!std::isfinite(cur)) throw std::overflow_error("assignment reduced cost overflow");
                        if(cur<minv[j]) {minv[j]=cur;way[j]=j0;}
                    }
                    if(minv[j]<delta) {delta=minv[j];j1=j;}
                }
                if(!j1 || !std::isfinite(delta)) throw std::overflow_error("assignment solver failed");
                for(std::size_t j=0;j<=cols;++j)
                    if(used[j]) {u[p[j]]+=delta;v[j]-=delta;}
                    else if (std::isfinite(minv[j])) minv[j]-=delta;
                j0=j1;
            } while(p[j0]);
            do {
                const auto j1=way[j0];
                p[j0]=p[j1];
                j0=j1;
            } while(j0);
        }
        std::vector<bool> assigned(m,false);
        for(std::size_t j=1;j<=cols;++j) if(p[j]) {
            const auto i=p[j]-1;
            if(j<=m && costs[i][j-1] && *costs[i][j-1]<=options_.unmatched_penalty) {
                result.track_for_observation[i]=j-1;
                assigned[j-1]=true;
                result.total_cost+=*costs[i][j-1];
            } else result.total_cost+=options_.unmatched_penalty;
        }
        if (!std::isfinite(result.total_cost))
            throw std::overflow_error("assignment total cost overflow");
        for(std::size_t j=0;j<m;++j) if(!assigned[j]) result.unmatched_tracks.push_back(j);
        return result;
    }
private:
    BatchAssignmentOptions options_{};
};
} // namespace audition::temporal

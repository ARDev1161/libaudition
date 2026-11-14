#include <audition/dsp/directional_candidate.hpp>
#include <array>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>

int main() {
    using audition::dsp::DirectionalCandidateExtractor;
    constexpr std::size_t n = 2048;
    std::array<std::vector<float>, 4> mic;
    const std::array<double, 4> positions{{.032, 0, -.032, 0}};
    // Plane wave arriving from +X: each mic receives a shifted common tone.
    // The reference is synthetic, not evidence of real speech separation.
    for (std::size_t m = 0; m < 4; ++m) {
        mic[m].resize(n);
        for (std::size_t i = 0; i < n; ++i)
            mic[m][i] = static_cast<float>(
                std::sin(2.0 * 3.141592653589793 * 1300.0 *
                         (static_cast<double>(i) / 16000.0 +
                          positions[m] / 343.0)));
    }
    std::array<const float*, 4> ptrs{};
    for (std::size_t m = 0; m < 4; ++m) ptrs[m] = mic[m].data();
    auto correct = DirectionalCandidateExtractor::extract(ptrs, n, 0.0, 16000);
    auto opposite = DirectionalCandidateExtractor::extract(
        ptrs, n, 3.141592653589793, 16000);
    double correct_energy = 0, opposite_energy = 0;
    for (std::size_t i = 16; i < n - 16; ++i) {
        correct_energy += correct[i] * correct[i];
        opposite_energy += opposite[i] * opposite[i];
    }
    assert(correct_energy > opposite_energy * 1.1);
    bool threw = false;
    try {
        DirectionalCandidateExtractor::extract(ptrs, n, 0.0, 0);
    } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
}

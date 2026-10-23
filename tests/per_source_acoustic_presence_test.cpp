#include "per_source_acoustic_presence.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    demo::PerSourceAcousticPresence gate;
    constexpr std::uint32_t rate = 16000;
    std::vector<float> quiet(160, 0.001f);
    std::vector<float> speech(160);
    // Stable background should eventually retire this track, without
    // affecting a second independently active source.
    bool alive = true;
    for (int i = 0; i < 500; ++i)
        alive = gate.update(1, quiet.data(), quiet.size(), rate);
    if (alive || gate.present(1)) return 1;
    // Intermittent speech from another source must preserve its activity.
    for (int i = 0; i < 500; ++i) {
        for (std::size_t j = 0; j < speech.size(); ++j)
            speech[j] = (i % 50 < 30 ? 0.02f : 0.001f);
        gate.update(2, speech.data(), speech.size(), rate);
    }
    if (!gate.present(2) || gate.present(1)) return 2;
    std::fill(speech.begin(), speech.end(), 0.02f);
    for (int i = 0; i < 100; ++i)
        alive = gate.update(1, speech.data(), speech.size(), rate);
    if (!alive || !gate.present(1)) return 3;
    gate.clear();
    if (!gate.present(1) || !gate.present(2)) return 4;
    std::cout << "per-source acoustic presence PASS\n";
    return 0;
}

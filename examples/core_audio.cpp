#include <acoustic/acoustic.hpp>

#include <iostream>
#include <vector>

int main() {
    acoustic::AudioFormat format{16000, 2, acoustic::AudioLayout::Interleaved};
    std::vector<float> samples{0.1F, -0.1F, 0.2F, -0.2F};
    acoustic::AudioBuffer buffer{std::move(samples), format, acoustic::Timestamp::monotonicNow(), 1};

    std::cout << "frames=" << buffer.frameCount() << " duration=" << buffer.duration().seconds()
              << "s ch0[1]=" << buffer.channel(0)[1] << '\n';
    return 0;
}

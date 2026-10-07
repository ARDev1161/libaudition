#include <audition/audition.hpp>

#include <iostream>
#include <vector>

int main() {
    audition::AudioFormat format{16000, 2, audition::AudioLayout::Interleaved};
    std::vector<float> samples{0.1F, -0.1F, 0.2F, -0.2F};
    audition::AudioBuffer buffer{std::move(samples), format, audition::Timestamp::monotonicNow(), 1};

    std::cout << "frames=" << buffer.frameCount() << " duration=" << buffer.duration().seconds()
              << "s ch0[1]=" << buffer.channel(0)[1] << '\n';
    return 0;
}

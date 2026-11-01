#include <audition/backends/whispercpp/asr.hpp>

#include <stdexcept>
#include <string>

int main() {
    try {
        audition::WhisperCppAsrOptions config{};
        config.threads = 2;
        audition::WhisperCppAsr engine{config};
        return 1; // Missing model must be rejected.
    } catch (const std::invalid_argument&) {
        return 0;
    } catch (...) {
        return 2;
    }
}

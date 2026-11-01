#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace demo {

// Qt-only ODAS pipeline diagnostics, collected per processed hop, NOT per
// 20-Hz UI frame. SSL candidates are raw scores, not confirmed tracks.
struct OdasStageStatistics {
    std::uint64_t processed_hops{0U};
    std::uint64_t positive_ssl_hops{0U};
    std::uint64_t sst_track_hops{0U};
    std::uint64_t sss_audio_hops{0U};
    std::uint64_t ssl_without_sst_hops{0U};
    std::uint64_t consecutive_ssl_without_sst_hops{0U};
    std::uint64_t longest_ssl_without_sst_hops{0U};
    std::uint64_t sst_without_sss_hops{0U};
    std::optional<std::uint64_t> first_positive_ssl_hop{};
    std::optional<std::uint64_t> first_sst_track_hop{};
    std::optional<std::uint64_t> first_sss_audio_hop{};
};

// Does not infer that an SSL proposal and a later SST track refer to the
// same physical source. Caller resets after a discontinuous ALSA/ODAS reset.
class OdasStageMonitor final {
public:
    void observe(bool positive_ssl, bool has_sst_track, bool has_sss_audio) noexcept {
        const auto current = state_.processed_hops;
        if (positive_ssl) {
            add(state_.positive_ssl_hops);
            if (!state_.first_positive_ssl_hop)
                state_.first_positive_ssl_hop = current;
        }
        if (has_sst_track) {
            add(state_.sst_track_hops);
            if (!state_.first_sst_track_hop)
                state_.first_sst_track_hop = current;
        }
        if (has_sss_audio) {
            add(state_.sss_audio_hops);
            if (!state_.first_sss_audio_hop)
                state_.first_sss_audio_hop = current;
        }
        if (positive_ssl && !has_sst_track) {
            add(state_.ssl_without_sst_hops);
            add(state_.consecutive_ssl_without_sst_hops);
            if (state_.longest_ssl_without_sst_hops <
                state_.consecutive_ssl_without_sst_hops)
                state_.longest_ssl_without_sst_hops =
                    state_.consecutive_ssl_without_sst_hops;
        } else {
            state_.consecutive_ssl_without_sst_hops = 0U;
        }
        if (has_sst_track && !has_sss_audio)
            add(state_.sst_without_sss_hops);
        add(state_.processed_hops);
    }

    void reset() noexcept { state_ = {}; }
    [[nodiscard]] const OdasStageStatistics& snapshot() const noexcept {
        return state_;
    }

private:
    static void add(std::uint64_t& counter) noexcept {
        if (counter != std::numeric_limits<std::uint64_t>::max()) ++counter;
    }
    OdasStageStatistics state_{};
};

} // namespace demo

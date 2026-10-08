#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <audition/core/probability.hpp>
#include <audition/interfaces/memory.hpp>
#include <audition/memory/types.hpp>

namespace audition {

struct SoundPrototypeMatchOptions {
    std::size_t max_results{5U};
    std::optional<Score> min_similarity{};
};

class CosineSoundPrototypeMatcher {
public:
    explicit CosineSoundPrototypeMatcher(const ISoundPrototypeRegistry& registry) noexcept;

    [[nodiscard]] std::vector<SoundPrototypeMatch> match(
        const AudioEmbedding& query,
        const SoundPrototypeMatchOptions& options = {}) const;

private:
    const ISoundPrototypeRegistry* registry_{nullptr};
};

}  // namespace audition

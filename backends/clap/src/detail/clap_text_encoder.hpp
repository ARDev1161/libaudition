#pragma once

#include <memory>
#include <string>
#include <vector>

#include <audition/backends/clap/options.hpp>

namespace audition::clap_detail {

class ClapTextEncoder {
public:
    explicit ClapTextEncoder(ClapOnnxTextOptions options);
    ~ClapTextEncoder();

    ClapTextEncoder(const ClapTextEncoder&) = delete;
    ClapTextEncoder& operator=(const ClapTextEncoder&) = delete;
    ClapTextEncoder(ClapTextEncoder&&) noexcept;
    ClapTextEncoder& operator=(ClapTextEncoder&&) noexcept;

    [[nodiscard]] std::vector<float> embed(
        const std::string& text) const;

    [[nodiscard]] const ClapOnnxTextOptions& options() const noexcept;
    [[nodiscard]] std::size_t vocabularySize() const noexcept;
    [[nodiscard]] std::int64_t bosId() const noexcept;
    [[nodiscard]] std::int64_t eosId() const noexcept;
    [[nodiscard]] std::int64_t padId() const noexcept;
    [[nodiscard]] std::int64_t unkId() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace audition::clap_detail

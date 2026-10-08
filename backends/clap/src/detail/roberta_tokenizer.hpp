#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace audition::clap_detail {

struct TokenizedText {
    std::vector<std::int64_t> input_ids{};
    std::vector<std::int64_t> attention_mask{};
};

class RobertaTokenizer {
public:
    explicit RobertaTokenizer(
        const std::filesystem::path& tokenizer_json);

    [[nodiscard]] TokenizedText encode(
        const std::string& text,
        std::size_t max_length) const;

    [[nodiscard]] std::int64_t bosId() const noexcept;
    [[nodiscard]] std::int64_t eosId() const noexcept;
    [[nodiscard]] std::int64_t padId() const noexcept;
    [[nodiscard]] std::int64_t unkId() const noexcept;
    [[nodiscard]] std::size_t vocabSize() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace audition::clap_detail

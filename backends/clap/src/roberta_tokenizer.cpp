#include "detail/roberta_tokenizer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <audition/core/error.hpp>

#include <nlohmann/json.hpp>

namespace audition::clap_detail {
namespace {

std::string encodeUtf8(std::uint32_t codepoint) {
    std::string out;
    if (codepoint < 0x80U) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        out.push_back(static_cast<char>(
            0x80U | (codepoint & 0x3FU)));
    } else if (codepoint < 0x10000U) {
        out.push_back(static_cast<char>(
            0xE0U | (codepoint >> 12U)));
        out.push_back(static_cast<char>(
            0x80U | ((codepoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(
            0x80U | (codepoint & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(
            0xF0U | (codepoint >> 18U)));
        out.push_back(static_cast<char>(
            0x80U | ((codepoint >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(
            0x80U | ((codepoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(
            0x80U | (codepoint & 0x3FU)));
    }
    return out;
}

bool byteIsDirect(std::uint32_t byte) {
    return (byte >= 33U && byte <= 126U) ||
           (byte >= 161U && byte <= 172U) ||
           (byte >= 174U && byte <= 255U);
}

std::array<std::string, 256U> buildByteEncoder() {
    std::array<std::string, 256U> table{};
    std::uint32_t remapped = 0U;
    for (std::uint32_t byte = 0U; byte < 256U; ++byte) {
        if (byteIsDirect(byte)) {
            table[byte] = encodeUtf8(byte);
        } else {
            table[byte] = encodeUtf8(256U + remapped);
            ++remapped;
        }
    }
    return table;
}

const std::array<std::string, 256U>& byteEncoder() {
    static const auto table = buildByteEncoder();
    return table;
}

std::size_t utf8CharLength(unsigned char lead) {
    if ((lead & 0x80U) == 0U) {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 1U;
}

const std::regex& preTokenRegex() {
    static const std::regex expression{
        R"('s|'t|'re|'ve|'m|'ll|'d| ?[A-Za-z]+| ?[0-9]+| ?[^\sA-Za-z0-9]+|\s+(?!\S)|\s+)",
        std::regex::ECMAScript};
    return expression;
}

void requireAscii(const std::string& text) {
    for (unsigned char byte : text) {
        if (byte >= 0x80U) {
            throw Error{
                ErrorCode::UnsupportedFormat,
                "CLAP RoBERTa tokenizer currently supports ASCII candidate labels only"};
        }
    }
}

}  // namespace

class RobertaTokenizer::Impl {
public:
    struct PairHash {
        std::size_t operator()(
            const std::pair<std::string, std::string>& pair) const noexcept {
            const auto first =
                std::hash<std::string>{}(pair.first);
            const auto second =
                std::hash<std::string>{}(pair.second);
            return first ^ (second << 1U);
        }
    };

    explicit Impl(const std::filesystem::path& path) {
        std::ifstream stream(path);
        if (!stream) {
            throw Error{
                ErrorCode::ModelLoadError,
                "CLAP tokenizer.json could not be opened"};
        }

        nlohmann::json document;
        try {
            stream >> document;
        } catch (const nlohmann::json::exception& exception) {
            throw Error{
                ErrorCode::ModelLoadError,
                std::string{"CLAP tokenizer.json parse failed: "} +
                    exception.what()};
        }

        if (!document.contains("model") ||
            !document["model"].is_object()) {
            throw Error{
                ErrorCode::ModelLoadError,
                "CLAP tokenizer.json is missing the model object"};
        }

        const auto& model = document["model"];
        if (model.contains("type") &&
            model["type"].is_string() &&
            model["type"].get<std::string>() != "BPE") {
            throw Error{
                ErrorCode::ModelLoadError,
                "CLAP tokenizer model must use byte-level BPE"};
        }
        if (!model.contains("vocab") ||
            !model["vocab"].is_object()) {
            throw Error{
                ErrorCode::ModelLoadError,
                "CLAP tokenizer.json is missing model.vocab"};
        }
        if (!model.contains("merges") ||
            !model["merges"].is_array()) {
            throw Error{
                ErrorCode::ModelLoadError,
                "CLAP tokenizer.json is missing model.merges"};
        }

        const auto& vocab_json = model["vocab"];
        vocab.reserve(vocab_json.size());
        for (auto iterator = vocab_json.begin();
             iterator != vocab_json.end();
             ++iterator) {
            if (!iterator.value().is_number_integer()) {
                throw Error{
                    ErrorCode::ModelLoadError,
                    "CLAP tokenizer vocabulary IDs must be integers"};
            }
            vocab.emplace(
                iterator.key(),
                iterator.value().get<std::int64_t>());
        }

        const auto& merges_json = model["merges"];
        merges.reserve(merges_json.size());
        std::int32_t rank = 0;
        for (const auto& entry : merges_json) {
            std::string first;
            std::string second;

            if (entry.is_array() && entry.size() >= 2U &&
                entry[0].is_string() && entry[1].is_string()) {
                first = entry[0].get<std::string>();
                second = entry[1].get<std::string>();
            } else if (entry.is_string()) {
                const auto merged = entry.get<std::string>();
                const auto separator = merged.find(' ');
                if (separator == std::string::npos) {
                    ++rank;
                    continue;
                }
                first = merged.substr(0U, separator);
                second = merged.substr(separator + 1U);
            } else {
                ++rank;
                continue;
            }

            merges.emplace(
                std::pair{std::move(first), std::move(second)},
                rank);
            ++rank;
        }

        bos_id = tokenId("<s>");
        eos_id = tokenId("</s>");
        pad_id = tokenId("<pad>");
        unk_id = tokenId("<unk>");

        if (bos_id < 0 || eos_id < 0 ||
            pad_id < 0 || unk_id < 0) {
            throw Error{
                ErrorCode::ModelLoadError,
                "CLAP tokenizer is missing required RoBERTa special tokens"};
        }
    }

    [[nodiscard]] std::int64_t tokenId(
        const std::string& token) const {
        const auto iterator = vocab.find(token);
        return iterator == vocab.end() ? -1 : iterator->second;
    }

    [[nodiscard]] std::vector<std::string> bpe(
        const std::string& token) const {
        {
            const std::lock_guard<std::mutex> lock{cache_mutex};
            const auto cached = cache.find(token);
            if (cached != cache.end()) {
                return cached->second;
            }
        }

        std::vector<std::string> symbols;
        for (std::size_t offset = 0U;
             offset < token.size();) {
            const auto length =
                utf8CharLength(
                    static_cast<unsigned char>(token[offset]));
            symbols.push_back(token.substr(offset, length));
            offset += length;
        }

        while (symbols.size() > 1U) {
            auto best_rank =
                std::numeric_limits<std::int32_t>::max();
            std::pair<std::string, std::string> best_pair{};

            for (std::size_t i = 0U;
                 i + 1U < symbols.size();
                 ++i) {
                const auto iterator =
                    merges.find({symbols[i], symbols[i + 1U]});
                if (iterator != merges.end() &&
                    iterator->second < best_rank) {
                    best_rank = iterator->second;
                    best_pair = iterator->first;
                }
            }

            if (best_rank ==
                std::numeric_limits<std::int32_t>::max()) {
                break;
            }

            std::vector<std::string> merged;
            merged.reserve(symbols.size());
            std::size_t i = 0U;
            while (i < symbols.size()) {
                if (i + 1U < symbols.size() &&
                    symbols[i] == best_pair.first &&
                    symbols[i + 1U] == best_pair.second) {
                    merged.push_back(
                        symbols[i] + symbols[i + 1U]);
                    i += 2U;
                } else {
                    merged.push_back(symbols[i]);
                    ++i;
                }
            }
            symbols = std::move(merged);
        }

        {
            const std::lock_guard<std::mutex> lock{cache_mutex};
            cache.emplace(token, symbols);
        }
        return symbols;
    }

    std::unordered_map<std::string, std::int64_t> vocab{};
    std::unordered_map<
        std::pair<std::string, std::string>,
        std::int32_t,
        PairHash> merges{};
    std::int64_t bos_id{-1};
    std::int64_t eos_id{-1};
    std::int64_t pad_id{-1};
    std::int64_t unk_id{-1};

    mutable std::mutex cache_mutex{};
    mutable std::unordered_map<
        std::string,
        std::vector<std::string>> cache{};
};

RobertaTokenizer::RobertaTokenizer(
    const std::filesystem::path& tokenizer_json)
    : impl_(std::make_shared<Impl>(tokenizer_json)) {}

TokenizedText RobertaTokenizer::encode(
    const std::string& text,
    std::size_t max_length) const {
    if (max_length < 2U) {
        throw Error{
            ErrorCode::InvalidArgument,
            "CLAP tokenizer sequence length must be at least two"};
    }
    requireAscii(text);

    std::vector<std::int64_t> content_ids;
    content_ids.reserve(32U);

    const auto& bytes = byteEncoder();
    for (auto iterator = std::sregex_iterator(
             text.begin(), text.end(), preTokenRegex());
         iterator != std::sregex_iterator{};
         ++iterator) {
        const auto pretoken = iterator->str();

        std::string encoded;
        encoded.reserve(pretoken.size() * 2U);
        for (unsigned char byte : pretoken) {
            encoded += bytes[byte];
        }

        for (const auto& symbol : impl_->bpe(encoded)) {
            const auto token_id = impl_->tokenId(symbol);
            content_ids.push_back(
                token_id >= 0 ? token_id : impl_->unk_id);
        }
    }

    const auto content_capacity = max_length - 2U;
    if (content_ids.size() > content_capacity) {
        content_ids.resize(content_capacity);
    }

    TokenizedText result;
    result.input_ids.reserve(max_length);
    result.attention_mask.reserve(max_length);

    result.input_ids.push_back(impl_->bos_id);
    result.attention_mask.push_back(1);

    for (const auto token_id : content_ids) {
        result.input_ids.push_back(token_id);
        result.attention_mask.push_back(1);
    }

    result.input_ids.push_back(impl_->eos_id);
    result.attention_mask.push_back(1);

    while (result.input_ids.size() < max_length) {
        result.input_ids.push_back(impl_->pad_id);
        result.attention_mask.push_back(0);
    }

    return result;
}

std::int64_t RobertaTokenizer::bosId() const noexcept {
    return impl_->bos_id;
}

std::int64_t RobertaTokenizer::eosId() const noexcept {
    return impl_->eos_id;
}

std::int64_t RobertaTokenizer::padId() const noexcept {
    return impl_->pad_id;
}

std::int64_t RobertaTokenizer::unkId() const noexcept {
    return impl_->unk_id;
}

std::size_t RobertaTokenizer::vocabSize() const noexcept {
    return impl_->vocab.size();
}

}  // namespace audition::clap_detail

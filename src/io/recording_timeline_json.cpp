#include <audition/audio/recording_timeline_json.hpp>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace audition {
namespace {

// Small strict JSON reader; deliberately only the standard types and escape
// sequences, so installed libaudition has no external JSON dependency.
struct Json {
    enum Kind { Object, Array, String, Number, Boolean, Null } kind{Null};
    std::map<std::string, Json> object{};
    std::vector<Json> array{};
    std::string value{};
};
class Parser {
public:
    explicit Parser(std::string_view source) : source_(source) {}
    Json parse() {
        auto root = value(0);
        ws();
        if (pos_ != source_.size()) fail("Trailing JSON data");
        return root;
    }
private:
    [[noreturn]] static void fail(const char* what) {
        throw std::invalid_argument(what);
    }
    void ws() {
        while (pos_ < source_.size() &&
               (source_[pos_] == ' ' || source_[pos_] == '\t' ||
                source_[pos_] == '\n' || source_[pos_] == '\r')) ++pos_;
    }
    bool consume(char ch) {
        ws();
        if (pos_ < source_.size() && source_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }
    void require(char ch) {
        if (!consume(ch)) fail("Malformed JSON delimiter");
    }
    char take() {
        if (pos_ >= source_.size()) fail("Truncated JSON");
        return source_[pos_++];
    }
    void literal(std::string_view rest) {
        for (const auto ch : rest)
            if (take() != ch) fail("Invalid JSON literal");
    }
    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 63)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (cp & 63)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (cp & 63)));
        }
    }
    std::uint32_t hex4() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const auto digit = take();
            v <<= 4;
            if (digit >= '0' && digit <= '9')
                v |= static_cast<std::uint32_t>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                v |= static_cast<std::uint32_t>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                v |= static_cast<std::uint32_t>(digit - 'A' + 10);
            else fail("Invalid Unicode escape");
        }
        return v;
    }
    std::string string() {
        if (take() != '"') fail("Expected JSON string");
        std::string out;
        while (true) {
            const auto ch = take();
            if (ch == '"') return out;
            if (static_cast<unsigned char>(ch) < 32U)
                fail("Unescaped JSON control character");
            if (ch != '\\') { out.push_back(ch); continue; }
            const auto escaped = take();
            switch (escaped) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                auto cp = hex4();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (take() != '\\' || take() != 'u')
                        fail("Missing low Unicode surrogate");
                    const auto low = hex4();
                    if (low < 0xdc00 || low > 0xdfff)
                        fail("Invalid low Unicode surrogate");
                    cp = 0x10000U + ((cp - 0xd800U) << 10) + low - 0xdc00U;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                    fail("Unpaired Unicode surrogate");
                }
                appendUtf8(out, cp);
                break;
            }
            default: fail("Invalid JSON escape");
            }
        }
    }
    Json number() {
        Json out;
        out.kind = Json::Number;
        const auto start = pos_;
        if (pos_ < source_.size() && source_[pos_] == '-') ++pos_;
        if (pos_ >= source_.size()) fail("Truncated JSON number");
        if (source_[pos_] == '0') ++pos_;
        else {
            if (source_[pos_] < '1' || source_[pos_] > '9')
                fail("Invalid JSON number");
            while (pos_ < source_.size() &&
                   source_[pos_] >= '0' && source_[pos_] <= '9') ++pos_;
        }
        if (pos_ < source_.size() && source_[pos_] == '.') {
            ++pos_;
            const auto before = pos_;
            while (pos_ < source_.size() &&
                   source_[pos_] >= '0' && source_[pos_] <= '9') ++pos_;
            if (before == pos_) fail("Invalid JSON decimal");
        }
        if (pos_ < source_.size() &&
            (source_[pos_] == 'e' || source_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < source_.size() &&
                (source_[pos_] == '+' || source_[pos_] == '-')) ++pos_;
            const auto before = pos_;
            while (pos_ < source_.size() &&
                   source_[pos_] >= '0' && source_[pos_] <= '9') ++pos_;
            if (before == pos_) fail("Invalid JSON exponent");
        }
        out.value = std::string{source_.substr(start, pos_ - start)};
        return out;
    }
    Json value(unsigned depth) {
        if (depth > 48) fail("JSON nesting limit reached");
        ws();
        if (pos_ >= source_.size()) fail("Missing JSON value");
        const auto ch = source_[pos_];
        if (ch == '{') {
            ++pos_;
            Json out; out.kind = Json::Object;
            if (consume('}')) return out;
            do {
                ws();
                if (pos_ >= source_.size() || source_[pos_] != '"')
                    fail("Expected JSON object key");
                const auto key = string();
                require(':');
                const auto entry = value(depth + 1);
                if (!out.object.emplace(key, entry).second)
                    fail("Duplicate JSON object key");
                if (consume('}')) return out;
                require(',');
            } while (true);
        }
        if (ch == '[') {
            ++pos_;
            Json out; out.kind = Json::Array;
            if (consume(']')) return out;
            do {
                out.array.push_back(value(depth + 1));
                if (consume(']')) return out;
                require(',');
            } while (true);
        }
        if (ch == '"') {
            Json out; out.kind = Json::String;
            out.value = string(); return out;
        }
        if (ch == 't') {
            ++pos_; literal("rue"); Json out; out.kind = Json::Boolean; return out;
        }
        if (ch == 'f') {
            ++pos_; literal("alse"); Json out; out.kind = Json::Boolean; return out;
        }
        if (ch == 'n') {
            ++pos_; literal("ull"); return {};
        }
        return number();
    }
    std::string_view source_;
    std::size_t pos_{0};
};
const Json& field(const Json& obj, const char* key, Json::Kind kind) {
    if (obj.kind != Json::Object) throw std::invalid_argument("JSON expected object");
    const auto it = obj.object.find(key);
    if (it == obj.object.end() || it->second.kind != kind)
        throw std::invalid_argument(std::string{"Invalid JSON field: "} + key);
    return it->second;
}
std::uint64_t integer(const Json& json) {
    if (json.kind != Json::Number || json.value.empty() ||
        json.value.front() == '-')
        throw std::invalid_argument("Expected nonnegative JSON integer");
    std::size_t parsed = 0;
    std::uint64_t n = 0;
    try {
        n = std::stoull(json.value, &parsed, 10);
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("JSON integer exceeds uint64 range");
    }
    if (parsed != json.value.size())
        throw std::invalid_argument("Expected integral JSON number");
    return n;
}
std::uint64_t count(const Json& obj, const char* key) {
    return integer(field(obj, key, Json::Number));
}
double probability(const Json& obj) {
    const auto& val = field(obj, "confidence", Json::Number);
    std::istringstream parser(val.value);
    parser.imbue(std::locale::classic());
    double out = 0;
    parser >> out;
    if (!parser || !(parser >> std::ws).eof() || !std::isfinite(out) ||
        out < 0 || out > 1)
        throw std::invalid_argument("Invalid JSON probability");
    return out;
}
} // namespace

RecordingTimeline recordingTimelineFromJson(std::string_view text) {
    if (text.size() > (64U * 1024U * 1024U))
        throw std::invalid_argument("Recording JSON exceeds size limit");
    const auto root = Parser{text}.parse();
    if (field(root, "schema", Json::String).value !=
        "libaudition.recording-timeline.v1")
        throw std::invalid_argument("Unsupported recording JSON schema");
    const auto id = count(root, "recording_id");
    const auto rate = count(root, "sample_rate_hz");
    if (!id || !rate || rate > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Invalid recording metadata");
    RecordingTimeline timeline{id, static_cast<std::uint32_t>(rate)};
    for (const auto& item : field(root, "speech", Json::Array).array) {
        timeline.add(TimelineSpeechEvent{id, count(item, "start_sample"),
            count(item, "end_sample"), field(item, "text", Json::String).value,
            field(item, "language", Json::String).value});
    }
    for (const auto& item : field(root, "sounds", Json::Array).array) {
        timeline.add(TimelineSoundEvent{id, count(item, "start_sample"),
            count(item, "end_sample"), field(item, "label", Json::String).value,
            field(item, "model", Json::String).value, probability(item),
            field(item, "stage", Json::String).value});
    }
    if (root.object.count("gaps")) {
        for (const auto& item : field(root, "gaps", Json::Array).array) {
            timeline.add(TimelineGapEvent{id, count(item, "start_sample"),
                count(item, "end_sample"), field(item, "reason", Json::String).value});
        }
    }
    return timeline;
}
RecordingTimeline readRecordingTimelineJson(const std::filesystem::path& file) {
    const auto size = std::filesystem::file_size(file);
    if (size > 64U * 1024U * 1024U)
        throw std::invalid_argument("Recording JSON exceeds size limit");
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open recording timeline");
    const std::string text{std::istreambuf_iterator<char>{in},
                           std::istreambuf_iterator<char>{}};
    if (!in.eof() && !in) throw std::runtime_error("Cannot read recording timeline");
    return recordingTimelineFromJson(text);
}
} // namespace audition

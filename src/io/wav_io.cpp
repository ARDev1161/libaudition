#include <audition/io/wav_io.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace audition::io {
namespace {

std::uint16_t readU16(std::istream& input) {
    std::array<unsigned char, 2> bytes{};
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        throw std::runtime_error{"Unexpected end of WAV file"};
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0]) |
        (static_cast<std::uint16_t>(bytes[1]) << 8U));
}

std::uint32_t readU32(std::istream& input) {
    std::array<unsigned char, 4> bytes{};
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        throw std::runtime_error{"Unexpected end of WAV file"};
    }
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U));
}

void writeU16(std::ostream& output, std::uint16_t value) {
    const std::array<unsigned char, 2> bytes{
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8U) & 0xffU)};
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ostream& output, std::uint32_t value) {
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8U) & 0xffU),
        static_cast<unsigned char>((value >> 16U) & 0xffU),
        static_cast<unsigned char>((value >> 24U) & 0xffU)};
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

std::string readTag(std::istream& input) {
    std::array<char, 4> tag{};
    input.read(tag.data(), static_cast<std::streamsize>(tag.size()));
    if (!input) {
        throw std::runtime_error{"Unexpected end of WAV file"};
    }
    return std::string{tag.data(), tag.size()};
}

float decodePcm(
    const unsigned char* data,
    std::uint16_t bits_per_sample) {
    switch (bits_per_sample) {
    case 8U:
        return static_cast<float>(
            (static_cast<int>(data[0]) - 128) / 128.0);
    case 16U: {
        const std::uint16_t raw = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[0]) |
            (static_cast<std::uint16_t>(data[1]) << 8U));
        const auto value = static_cast<std::int16_t>(raw);
        return static_cast<float>(
            static_cast<double>(value) / 32768.0);
    }
    case 24U: {
        std::int32_t value =
            static_cast<std::int32_t>(data[0]) |
            (static_cast<std::int32_t>(data[1]) << 8) |
            (static_cast<std::int32_t>(data[2]) << 16);
        if ((value & 0x00800000) != 0) {
            value |= static_cast<std::int32_t>(0xff000000);
        }
        return static_cast<float>(
            static_cast<double>(value) / 8388608.0);
    }
    case 32U: {
        const std::uint32_t raw =
            static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8U) |
            (static_cast<std::uint32_t>(data[2]) << 16U) |
            (static_cast<std::uint32_t>(data[3]) << 24U);
        const auto value = static_cast<std::int32_t>(raw);
        return static_cast<float>(
            static_cast<double>(value) / 2147483648.0);
    }
    default:
        throw std::runtime_error{
            "Unsupported PCM bit depth: " +
            std::to_string(bits_per_sample)};
    }
}

float decodeFloat32(const unsigned char* data) {
    static_assert(sizeof(float) == 4U);
    std::uint32_t raw =
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U);
    float value = 0.0F;
    std::memcpy(&value, &raw, sizeof(value));
    if (!std::isfinite(value)) {
        throw std::runtime_error{"WAV contains non-finite float sample"};
    }
    return value;
}

}  // namespace

LoadedWav loadWav(
    const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{
            "Cannot open WAV file: " + path.string()};
    }

    if (readTag(input) != "RIFF") {
        throw std::runtime_error{"Only RIFF WAV files are supported"};
    }
    static_cast<void>(readU32(input));
    if (readTag(input) != "WAVE") {
        throw std::runtime_error{"Invalid WAV signature"};
    }

    bool have_fmt = false;
    bool have_data = false;

    std::uint16_t format_tag = 0U;
    std::uint16_t channels = 0U;
    std::uint32_t sample_rate = 0U;
    std::uint16_t bits_per_sample = 0U;
    std::vector<unsigned char> data{};

    while (input && (!have_fmt || !have_data)) {
        const std::string chunk_id = readTag(input);
        const std::uint32_t chunk_size = readU32(input);

        if (chunk_id == "fmt ") {
            if (chunk_size < 16U) {
                throw std::runtime_error{"Invalid WAV fmt chunk"};
            }

            format_tag = readU16(input);
            channels = readU16(input);
            sample_rate = readU32(input);
            static_cast<void>(readU32(input));
            static_cast<void>(readU16(input));
            bits_per_sample = readU16(input);

            const std::uint32_t remaining = chunk_size - 16U;
            input.seekg(
                static_cast<std::streamoff>(remaining),
                std::ios::cur);
            have_fmt = true;
        } else if (chunk_id == "data") {
            // Validate the declared chunk against the actual file before
            // allocating. Otherwise a truncated 44-byte WAV can request
            // almost 4 GiB of memory from an untrusted RIFF length.
            const auto data_start = input.tellg();
            if (data_start == std::streampos{-1}) {
                throw std::runtime_error{"Cannot determine WAV data position"};
            }
            const auto available_bytes = std::filesystem::file_size(path);
            const auto start_offset = static_cast<std::uintmax_t>(
                static_cast<std::streamoff>(data_start));
            if (start_offset > available_bytes ||
                static_cast<std::uintmax_t>(chunk_size) >
                    available_bytes - start_offset) {
                throw std::runtime_error{"Truncated WAV data chunk"};
            }
            data.resize(chunk_size);
            input.read(
                reinterpret_cast<char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
            if (!input && !data.empty()) {
                throw std::runtime_error{"Truncated WAV data chunk"};
            }
            have_data = true;
        } else {
            input.seekg(
                static_cast<std::streamoff>(chunk_size),
                std::ios::cur);
        }

        if ((chunk_size & 1U) != 0U) {
            input.seekg(1, std::ios::cur);
        }
    }

    if (!have_fmt || !have_data) {
        throw std::runtime_error{"WAV is missing fmt or data chunk"};
    }
    if (channels == 0U || sample_rate == 0U) {
        throw std::runtime_error{"Invalid WAV channel count or sample rate"};
    }

    if (format_tag != 1U && format_tag != 3U) {
        throw std::runtime_error{
            "Unsupported WAV format tag: " +
            std::to_string(format_tag)};
    }

    if (format_tag == 3U && bits_per_sample != 32U) {
        throw std::runtime_error{
            "Only float32 IEEE WAV is supported"};
    }

    const std::size_t bytes_per_sample =
        static_cast<std::size_t>(bits_per_sample / 8U);
    if (bytes_per_sample == 0U ||
        bits_per_sample % 8U != 0U) {
        throw std::runtime_error{"Invalid WAV bit depth"};
    }
    if (data.size() % bytes_per_sample != 0U) {
        throw std::runtime_error{"WAV data size is not sample-aligned"};
    }

    const std::size_t sample_count =
        data.size() / bytes_per_sample;
    if (sample_count %
        static_cast<std::size_t>(channels) != 0U) {
        throw std::runtime_error{"WAV data is not frame-aligned"};
    }

    std::vector<float> samples;
    samples.reserve(sample_count);

    for (std::size_t offset = 0U;
         offset < data.size();
         offset += bytes_per_sample) {
        const auto* sample = data.data() + offset;
        const float value =
            format_tag == 1U
                ? decodePcm(sample, bits_per_sample)
                : decodeFloat32(sample);
        samples.push_back(value);
    }

    LoadedWav result{};
    result.audio = audition::AudioBuffer{
        std::move(samples),
        {
            sample_rate,
            static_cast<std::uint32_t>(channels),
            audition::AudioLayout::Interleaved,
        },
        audition::Timestamp{}};
    result.info.path = path;
    result.info.format_tag = format_tag;
    result.info.bits_per_sample = bits_per_sample;
    return result;
}

void saveWavPcm16(
    const std::filesystem::path& path,
    audition::AudioView audio) {
    if (!audio.format().valid()) {
        throw std::runtime_error{"Cannot save invalid audio format"};
    }
    if (audio.format().layout !=
        audition::AudioLayout::Interleaved) {
        throw std::runtime_error{
            "WAV writer requires interleaved audio"};
    }

    const std::uint64_t data_size_64 =
        static_cast<std::uint64_t>(audio.sampleCount()) * 2ULL;
    if (data_size_64 >
        std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error{"Audio is too large for RIFF/WAV"};
    }
    const auto data_size =
        static_cast<std::uint32_t>(data_size_64);

    std::ofstream output{path, std::ios::binary};
    if (!output) {
        throw std::runtime_error{
            "Cannot create WAV file: " + path.string()};
    }

    output.write("RIFF", 4);
    writeU32(output, 36U + data_size);
    output.write("WAVE", 4);

    output.write("fmt ", 4);
    writeU32(output, 16U);
    writeU16(output, 1U);
    writeU16(
        output,
        static_cast<std::uint16_t>(
            audio.format().channel_count));
    writeU32(output, audio.format().sample_rate_hz);

    const std::uint32_t byte_rate =
        audio.format().sample_rate_hz *
        audio.format().channel_count * 2U;
    writeU32(output, byte_rate);
    writeU16(
        output,
        static_cast<std::uint16_t>(
            audio.format().channel_count * 2U));
    writeU16(output, 16U);

    output.write("data", 4);
    writeU32(output, data_size);

    for (std::size_t index = 0U;
         index < audio.sampleCount();
         ++index) {
        const double clamped =
            std::clamp(
                static_cast<double>(audio.data()[index]),
                -1.0,
                1.0);
        const auto quantized =
            static_cast<std::int16_t>(
                std::lrint(
                    clamped *
                    (clamped < 0.0 ? 32768.0 : 32767.0)));
        writeU16(
            output,
            static_cast<std::uint16_t>(quantized));
    }

    if (!output) {
        throw std::runtime_error{"Failed while writing WAV file"};
    }
}

audition::AudioBuffer selectMonoChannel(
    audition::AudioView audio,
    std::size_t channel_index) {
    if (channel_index >=
        audio.format().channel_count) {
        throw std::runtime_error{
            "Requested WAV channel is out of range"};
    }

    std::vector<float> samples;
    samples.reserve(audio.frameCount());
    const auto channel = audio.channel(channel_index);
    for (std::size_t index = 0U;
         index < channel.size();
         ++index) {
        samples.push_back(channel[index]);
    }

    return audition::AudioBuffer{
        std::move(samples),
        {
            audio.format().sample_rate_hz,
            1U,
            audition::AudioLayout::Interleaved,
        },
        audio.captureTime(),
        audio.sequenceNumber()};
}

WavNormalizeResult normalizePcm16Wav(
    const std::filesystem::path& input, const std::filesystem::path& output,
    PeakNormalizeOptions options) {
    if (input == output) throw std::invalid_argument("WAV normalization must not overwrite its source");
    if (!std::isfinite(options.target_peak_dbfs) ||
        !std::isfinite(options.max_gain_db) ||
        options.target_peak_dbfs > 0.0 ||
        options.max_gain_db < 0.0 || options.max_gain_db > 120.0)
        throw std::invalid_argument("Invalid WAV normalization options");
    std::ifstream source(input, std::ios::binary);
    if (!source) throw std::runtime_error("Cannot open WAV for normalization");
    std::array<char, 44> header{};
    source.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!source || std::string(header.data(), 4) != "RIFF" ||
        std::string(header.data() + 8, 4) != "WAVE" ||
        std::string(header.data() + 12, 4) != "fmt " ||
        std::string(header.data() + 36, 4) != "data" ||
        static_cast<unsigned char>(header[20]) != 1U ||
        static_cast<unsigned char>(header[21]) != 0U ||
        static_cast<unsigned char>(header[22]) != 1U ||
        static_cast<unsigned char>(header[23]) != 0U ||
        static_cast<unsigned char>(header[34]) != 16U ||
        static_cast<unsigned char>(header[35]) != 0U)
        throw std::runtime_error("Normalization requires a canonical mono PCM16 WAV");
    const auto rate = static_cast<std::uint32_t>(
        static_cast<unsigned char>(header[24]) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[25])) << 8U) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[26])) << 16U) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[27])) << 24U));
    if (!rate) throw std::runtime_error("WAV sample rate is zero");
    const auto length = std::filesystem::file_size(input);
    if (length < header.size() || (length - header.size()) % 2 != 0)
        throw std::runtime_error("Invalid PCM16 WAV length");
    const auto count = (length - header.size()) / 2ULL;
    constexpr std::size_t kBatch = 4096U;
    std::array<char, kBatch * 2> bytes{};
    std::array<std::int16_t, kBatch> outputPcm{};
    auto readSample = [&](std::size_t i) {
        const auto lo = static_cast<unsigned char>(bytes[i * 2]);
        const auto hi = static_cast<unsigned char>(bytes[i * 2 + 1]);
        return static_cast<std::int16_t>(
            static_cast<std::uint16_t>(lo | (static_cast<unsigned>(hi) << 8U)));
    };
    std::uint32_t peak = 0;
    for (std::uint64_t left = count; left;) {
        const auto take = static_cast<std::size_t>(
            std::min<std::uint64_t>(left, kBatch));
        source.read(bytes.data(), static_cast<std::streamsize>(take * 2U));
        if (!source) throw std::runtime_error("Recording changed during normalization scan");
        for (std::size_t i = 0; i < take; ++i)
            peak = std::max(peak, static_cast<std::uint32_t>(
                std::abs(static_cast<int>(readSample(i)))));
        left -= take;
    }
    double gainDb = 0.0;
    if (peak) {
        const auto inputDb = 20.0 * std::log10(static_cast<double>(peak) / 32768.0);
        gainDb = std::clamp(options.target_peak_dbfs - inputDb,
            options.allow_attenuation ? -120.0 : 0.0, options.max_gain_db);
    }
    const auto scale = std::pow(10.0, gainDb / 20.0);
    source.clear();
    source.seekg(static_cast<std::streamoff>(header.size()));
    try {
        StreamingPcm16WavWriter dest(output, rate, 1);
        for (std::uint64_t left = count; left;) {
            const auto take = static_cast<std::size_t>(
                std::min<std::uint64_t>(left, kBatch));
            source.read(bytes.data(), static_cast<std::streamsize>(take * 2U));
            if (!source) throw std::runtime_error("Recording changed during normalization write");
            for (std::size_t i = 0; i < take; ++i) {
                const auto scaled = static_cast<long>(std::lrint(
                    static_cast<double>(readSample(i)) * scale));
                outputPcm[i] = static_cast<std::int16_t>(
                    std::clamp(scaled, -32768L, 32767L));
            }
            dest.append(outputPcm.data(), take);
            left -= take;
        }
        dest.finish();
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
        throw;
    }
    return {gainDb, count};
}

StreamingPcm16WavWriter::StreamingPcm16WavWriter(
    const std::filesystem::path& path, std::uint32_t rate, std::uint16_t channels)
    : stream_{path, std::ios::binary | std::ios::trunc},
      rate_(rate), channels_(channels) {
    if (!rate_ || !channels_ ||
        static_cast<std::uint64_t>(rate_) * channels_ * 2ULL >
            std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Invalid streaming WAV audio format");
    if (!stream_) throw std::runtime_error("Unable to create streaming WAV");
    writeHeader();
}
StreamingPcm16WavWriter::~StreamingPcm16WavWriter() noexcept {
    try { if (!finished_) finish(); } catch (...) {}
}
void StreamingPcm16WavWriter::writeHeader() {
    constexpr std::uint64_t maxBytes = 0xffffffffULL - 36ULL;
    if (samples_ > maxBytes / 2ULL)
        throw std::overflow_error("RIFF WAV 4GiB limit exceeded");
    const auto position = stream_.tellp();
    stream_.seekp(0);
    stream_.write("RIFF", 4);
    const auto bytes = static_cast<std::uint32_t>(samples_ * 2ULL);
    writeU32(stream_, 36U + bytes);
    stream_.write("WAVEfmt ", 8);
    writeU32(stream_, 16);
    writeU16(stream_, 1);
    writeU16(stream_, channels_);
    writeU32(stream_, rate_);
    writeU32(stream_, rate_ * static_cast<std::uint32_t>(channels_) * 2U);
    writeU16(stream_, static_cast<std::uint16_t>(channels_ * 2U));
    writeU16(stream_, 16);
    stream_.write("data", 4);
    writeU32(stream_, bytes);
    stream_.seekp(position < std::streampos(44) ? std::streampos(44) : position);
    if (!stream_) throw std::runtime_error("Cannot write streaming WAV header");
}
void StreamingPcm16WavWriter::append(
    const std::int16_t* pcm, std::size_t sample_count) {
    if (finished_ || (sample_count && !pcm) ||
        sample_count % channels_ != 0U)
        throw std::invalid_argument("Invalid PCM16 streaming append");
    constexpr std::uint64_t maxSamples = (0xffffffffULL - 36ULL) / 2ULL;
    if (sample_count > maxSamples - samples_)
        throw std::overflow_error("RIFF WAV 4GiB limit exceeded");
    for (std::size_t i = 0; i < sample_count; ++i)
        writeU16(stream_, static_cast<std::uint16_t>(pcm[i]));
    if (!stream_) throw std::runtime_error("Cannot append WAV PCM");
    samples_ += sample_count;
    // Keep WAV playable while recording; update header at least once per second.
    if (samples_ % rate_ < sample_count) flushHeader();
}
void StreamingPcm16WavWriter::appendSilence(std::uint64_t count) {
    if (finished_ || count % channels_ != 0ULL)
        throw std::invalid_argument("Invalid WAV silence append");
    constexpr std::uint64_t maxSamples = (0xffffffffULL - 36ULL) / 2ULL;
    if (count > maxSamples - samples_)
        throw std::overflow_error("RIFF WAV 4GiB limit exceeded");
    constexpr std::size_t chunkSamples = 4096U;
    const std::array<char, chunkSamples * 2> silence{};
    while (count > 0) {
        const auto take = std::min<std::uint64_t>(count, chunkSamples);
        stream_.write(silence.data(), static_cast<std::streamsize>(take * 2ULL));
        if (!stream_) throw std::runtime_error("Cannot append WAV silence");
        samples_ += take;
        count -= take;
    }
    flushHeader();
}
void StreamingPcm16WavWriter::flushHeader() {
    if (finished_) return;
    writeHeader();
    stream_.flush();
    if (!stream_) throw std::runtime_error("Cannot flush WAV header");
}
void StreamingPcm16WavWriter::finish() {
    if (finished_) return;
    flushHeader();
    stream_.close();
    if (!stream_) throw std::runtime_error("Cannot close WAV file");
    finished_ = true;
}

}  // namespace audition::io

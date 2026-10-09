#include "wav_io.hpp"

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

namespace demo {
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
        const std::uint16_t raw =
            static_cast<std::uint16_t>(data[0]) |
            (static_cast<std::uint16_t>(data[1]) << 8U);
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

}  // namespace demo

#include <audition/io/wav_io.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class WavIoTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
            ("libaudition-wav-io-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir_);
    }
    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(dir_, error);
    }
    std::filesystem::path file(const char* name) const { return dir_ / name; }
    static void put16(std::vector<unsigned char>& out, std::uint16_t value) {
        out.push_back(static_cast<unsigned char>(value & 255U));
        out.push_back(static_cast<unsigned char>((value >> 8U) & 255U));
    }
    static void put32(std::vector<unsigned char>& out, std::uint32_t value) {
        put16(out, static_cast<std::uint16_t>(value & 65535U));
        put16(out, static_cast<std::uint16_t>(value >> 16U));
    }
    std::filesystem::path fixture(const char* name, std::uint16_t format,
                                  std::uint16_t bits, std::uint16_t channels,
                                  const std::vector<unsigned char>& pcm) const {
        std::vector<unsigned char> bytes{'R','I','F','F'};
        put32(bytes, static_cast<std::uint32_t>(36U + pcm.size()));
        bytes.insert(bytes.end(), {'W','A','V','E','f','m','t',' '});
        put32(bytes, 16U);
        put16(bytes, format);
        put16(bytes, channels);
        put32(bytes, 16000U);
        const auto sample_bytes = static_cast<std::uint32_t>(bits / 8U);
        put32(bytes, 16000U * channels * sample_bytes);
        put16(bytes, static_cast<std::uint16_t>(channels * sample_bytes));
        put16(bytes, bits);
        bytes.insert(bytes.end(), {'d','a','t','a'});
        put32(bytes, static_cast<std::uint32_t>(pcm.size()));
        bytes.insert(bytes.end(), pcm.begin(), pcm.end());
        const auto path = file(name);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return path;
    }
    std::filesystem::path dir_;
};
} // namespace

TEST_F(WavIoTest, RoundTripsStereoPcm16WithSaturation) {
    audition::AudioBuffer input{{-2.F, 2.F, -0.5F, 0.5F},
        {16000, 2, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    audition::io::saveWavPcm16(file("roundtrip.wav"), input.view());
    const auto loaded = audition::io::loadWav(file("roundtrip.wav"));
    EXPECT_EQ(loaded.info.bits_per_sample, 16U);
    EXPECT_EQ(loaded.info.format_tag, 1U);
    ASSERT_EQ(loaded.audio.samples().size(), 4U);
    EXPECT_FLOAT_EQ(loaded.audio.samples()[0], -1.F);
    EXPECT_NEAR(loaded.audio.samples()[1], 32767.F / 32768.F, 1e-6);
    EXPECT_NEAR(loaded.audio.samples()[2], -0.5, 1e-6);
    EXPECT_NEAR(loaded.audio.samples()[3], 0.5, 1e-4);
    const auto right = audition::io::selectMonoChannel(loaded.audio.view(), 1U);
    ASSERT_EQ(right.samples().size(), 2U);
    EXPECT_FLOAT_EQ(right.samples()[0], loaded.audio.samples()[1]);
    EXPECT_FLOAT_EQ(right.samples()[1], loaded.audio.samples()[3]);
    EXPECT_THROW(static_cast<void>(audition::io::selectMonoChannel(loaded.audio.view(), 2U)), std::runtime_error);
}

TEST_F(WavIoTest, DecodesPcm8Pcm24Pcm32AndFloat32) {
    auto check = [&](const char* name, std::uint16_t format, std::uint16_t bits,
                     std::vector<unsigned char> pcm, float expected) {
        const auto loaded = audition::io::loadWav(fixture(name, format, bits, 1U, pcm));
        ASSERT_EQ(loaded.audio.samples().size(), 1U);
        EXPECT_NEAR(loaded.audio.samples()[0], expected, 1e-6);
    };
    check("pcm8.wav", 1, 8, {0}, -1.F);
    check("pcm24.wav", 1, 24, {0, 0, 0x80}, -1.F);
    check("pcm32.wav", 1, 32, {0, 0, 0, 0x80}, -1.F);
    check("float32.wav", 3, 32, {0, 0, 0, 0x3f}, 0.5F);
}

TEST_F(WavIoTest, RejectsMalformedAndUnsupportedWav) {
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(file("absent.wav"))), std::runtime_error);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(fixture("badformat.wav", 7, 16, 1, {0, 0}))), std::runtime_error);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(fixture("badbits.wav", 1, 12, 1, {0, 0}))), std::runtime_error);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(fixture("badfloat.wav", 3, 16, 1, {0, 0}))), std::runtime_error);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(fixture("badchannels.wav", 1, 16, 0, {0, 0}))), std::runtime_error);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(fixture("partial.wav", 1, 16, 1, {0}))), std::runtime_error);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(fixture("partialframe.wav", 1, 16, 2, {0, 0}))), std::runtime_error);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(fixture("nan.wav", 3, 32, 1, {0, 0, 0xc0, 0x7f}))), std::runtime_error);
    const auto truncated = fixture("truncated.wav", 1, 16, 1, {0, 0, 0, 0});
    std::filesystem::resize_file(truncated, 45U);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(truncated)), std::runtime_error);
}

TEST_F(WavIoTest, StreamingWriterRejectsInvalidAppendsAndFinalizesIdempotently) {
    EXPECT_THROW(audition::io::StreamingPcm16WavWriter(file("bad.wav"), 0),
                 std::invalid_argument);
    audition::io::StreamingPcm16WavWriter writer(file("stream.wav"), 16000, 2);
    const std::int16_t pcm[]{100, -100, 200, -200};
    EXPECT_THROW(writer.append(nullptr, 2U), std::invalid_argument);
    EXPECT_THROW(writer.append(pcm, 3U), std::invalid_argument);
    EXPECT_THROW(writer.appendSilence(3U), std::invalid_argument);
    writer.append(pcm, 4U);
    writer.appendSilence(2U);
    writer.flushHeader();
    writer.finish();
    writer.finish();
    EXPECT_EQ(writer.sampleCount(), 6U);
    EXPECT_THROW(writer.append(pcm, 2U), std::invalid_argument);
    const auto loaded = audition::io::loadWav(file("stream.wav"));
    ASSERT_EQ(loaded.audio.samples().size(), 6U);
    EXPECT_FLOAT_EQ(loaded.audio.samples()[4], 0.F);
    EXPECT_FLOAT_EQ(loaded.audio.samples()[5], 0.F);
}


TEST_F(WavIoTest, NormalizesMonoPcm16InBoundedBatchesWithoutChangingSource) {
    std::vector<float> samples(5000U, 0.125F);
    samples[2000] = -0.25F;
    audition::AudioBuffer original{samples,
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto input = file("mono-source.wav");
    const auto output = file("mono-normalized.wav");
    audition::io::saveWavPcm16(input, original.view());
    const auto before = audition::io::loadWav(input);
    const auto result = audition::io::normalizePcm16Wav(input, output);
    EXPECT_EQ(result.samples_written, samples.size());
    EXPECT_GT(result.gain_db, 0.0);
    EXPECT_LE(result.gain_db, 40.0);
    const auto after = audition::io::loadWav(output);
    ASSERT_EQ(after.audio.samples().size(), samples.size());
    EXPECT_GT(std::abs(after.audio.samples()[2000]),
              std::abs(before.audio.samples()[2000]));
    EXPECT_EQ(audition::io::loadWav(input).audio.samples(),
              before.audio.samples());
}

TEST_F(WavIoTest, NormalizationRespectsAttenuationAndSilence) {
    audition::AudioBuffer loud{{0.9F, -0.9F, 0.4F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto input = file("loud.wav");
    audition::io::saveWavPcm16(input, loud.view());
    audition::PeakNormalizeOptions options{};
    options.target_peak_dbfs = -12.0;
    const auto unchanged = audition::io::normalizePcm16Wav(
        input, file("no-attenuation.wav"), options);
    EXPECT_DOUBLE_EQ(unchanged.gain_db, 0.0);
    options.allow_attenuation = true;
    const auto attenuated = audition::io::normalizePcm16Wav(
        input, file("attenuated.wav"), options);
    EXPECT_LT(attenuated.gain_db, 0.0);
    const auto normalized = audition::io::loadWav(file("attenuated.wav"));
    ASSERT_EQ(normalized.audio.samples().size(), 3U);
    EXPECT_LT(std::abs(normalized.audio.samples()[0]), 0.3F);

    audition::AudioBuffer silent{{0.F, 0.F},
        {16000U, 1U, audition::AudioLayout::Interleaved}, audition::Timestamp{}};
    const auto silentInput = file("silent.wav");
    audition::io::saveWavPcm16(silentInput, silent.view());
    const auto quiet = audition::io::normalizePcm16Wav(silentInput, file("quiet.wav"));
    EXPECT_DOUBLE_EQ(quiet.gain_db, 0.0);
    EXPECT_EQ(quiet.samples_written, 2U);
    EXPECT_EQ(audition::io::loadWav(file("quiet.wav")).audio.samples(),
              silent.samples());
}

TEST_F(WavIoTest, RejectsInvalidNormalizationOptionsAndNoncanonicalInputs) {
    const auto input = fixture("mono.wav", 1, 16, 1, {0, 0});
    audition::PeakNormalizeOptions invalid{};
    invalid.target_peak_dbfs = 1.0;
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        input, file("invalid-gain.wav"), invalid)), std::invalid_argument);
    invalid = {};
    invalid.max_gain_db = -1.0;
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        input, file("invalid-limit.wav"), invalid)), std::invalid_argument);
    invalid = {};
    invalid.max_gain_db = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        input, file("nan-limit.wav"), invalid)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        input, input)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        file("missing.wav"), file("out.wav"))), std::runtime_error);
    const auto stereo = fixture("stereo.wav", 1, 16, 2, {0, 0, 0, 0});
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        stereo, file("bad-layout.wav"))), std::runtime_error);
}


namespace {
void overwriteWavByte(const std::filesystem::path& path,
                      std::streamoff offset, unsigned char value) {
    std::fstream io{path, std::ios::binary | std::ios::in | std::ios::out};
    io.seekp(offset);
    io.put(static_cast<char>(value));
}
std::vector<unsigned char> wavBytes(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    const auto size = input.tellg();
    std::vector<unsigned char> result(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()), size);
    return result;
}
void writeWavBytes(const std::filesystem::path& path,
                   const std::vector<unsigned char>& bytes) {
    std::ofstream output{path, std::ios::binary};
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}
} // namespace

TEST_F(WavIoTest, RejectsRiffAndWaveSignatureCorruption) {
    const auto riff = fixture("not-riff.wav", 1, 16, 1, {0, 0});
    overwriteWavByte(riff, 0, 'X');
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(riff)), std::runtime_error);

    const auto wave = fixture("not-wave.wav", 1, 16, 1, {0, 0});
    overwriteWavByte(wave, 8, 'X');
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(wave)), std::runtime_error);

    const auto short_file = file("short-riff.wav");
    writeWavBytes(short_file, {'R', 'I', 'F', 'F', 0});
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(short_file)),
                 std::runtime_error);
}

TEST_F(WavIoTest, RejectsUndersizedFmtAndMissingDataChunks) {
    const auto bad_fmt = fixture("short-fmt.wav", 1, 16, 1, {0, 0});
    overwriteWavByte(bad_fmt, 16, 8);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(bad_fmt)),
                 std::runtime_error);

    const auto missing_data = fixture("no-data.wav", 1, 16, 1, {0, 0});
    std::filesystem::resize_file(missing_data, 36);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(missing_data)),
                 std::runtime_error);

    const auto incomplete_tag = fixture("partial-tag.wav", 1, 16, 1, {0, 0});
    std::filesystem::resize_file(incomplete_tag, 38);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(incomplete_tag)),
                 std::runtime_error);
}

TEST_F(WavIoTest, SkipsOddSizedUnknownChunkWithPadding) {
    const auto path = fixture("odd-chunk.wav", 1, 16, 1, {0, 0, 0xff, 0x7f});
    auto bytes = wavBytes(path);
    bytes.insert(bytes.begin() + 12,
                 {'J', 'U', 'N', 'K', 3, 0, 0, 0, 1, 2, 3, 0});
    bytes[4] = static_cast<unsigned char>(bytes.size() - 8U);
    writeWavBytes(path, bytes);

    const auto loaded = audition::io::loadWav(path);
    ASSERT_EQ(loaded.audio.samples().size(), 2U);
    EXPECT_FLOAT_EQ(loaded.audio.samples()[0], 0.F);
    EXPECT_NEAR(loaded.audio.samples()[1], 32767.F / 32768.F, 1e-6F);
}

TEST_F(WavIoTest, ReadsDataChunkBeforeFormatChunk) {
    const auto path = fixture("data-first.wav", 1, 16, 1, {0, 0, 0, 0x40});
    const auto original = wavBytes(path);
    std::vector<unsigned char> reordered(original.begin(), original.begin() + 12);
    reordered.insert(reordered.end(), original.begin() + 36, original.end());
    reordered.insert(reordered.end(), original.begin() + 12, original.begin() + 36);
    writeWavBytes(path, reordered);

    const auto loaded = audition::io::loadWav(path);
    EXPECT_EQ(loaded.info.format_tag, 1U);
    ASSERT_EQ(loaded.audio.samples().size(), 2U);
    EXPECT_FLOAT_EQ(loaded.audio.samples()[0], 0.F);
    EXPECT_FLOAT_EQ(loaded.audio.samples()[1], 0.5F);
}

TEST_F(WavIoTest, RejectsZeroRateAndZeroOrUnsupportedBitDepths) {
    const auto zero_rate = fixture("rate-zero.wav", 1, 16, 1, {0, 0});
    for (std::streamoff offset = 24; offset < 28; ++offset)
        overwriteWavByte(zero_rate, offset, 0);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(zero_rate)),
                 std::runtime_error);

    const auto zero_bits = fixture("bits-zero.wav", 1, 16, 1, {0, 0});
    overwriteWavByte(zero_bits, 34, 0);
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(zero_bits)),
                 std::runtime_error);

    const auto unsupported = fixture("pcm40.wav", 1, 40, 1, {0, 0, 0, 0, 0});
    EXPECT_THROW(static_cast<void>(audition::io::loadWav(unsupported)),
                 std::runtime_error);
}

TEST_F(WavIoTest, DecodesPcm8MidpointAndPositivePcm24) {
    const auto pcm8 = audition::io::loadWav(
        fixture("pcm8-midpoint.wav", 1, 8, 1, {128, 255}));
    ASSERT_EQ(pcm8.audio.samples().size(), 2U);
    EXPECT_FLOAT_EQ(pcm8.audio.samples()[0], 0.F);
    EXPECT_NEAR(pcm8.audio.samples()[1], 127.F / 128.F, 1e-6F);

    const auto pcm24 = audition::io::loadWav(
        fixture("pcm24-positive.wav", 1, 24, 1, {0xff, 0xff, 0x7f}));
    ASSERT_EQ(pcm24.audio.samples().size(), 1U);
    EXPECT_NEAR(pcm24.audio.samples()[0],
                8388607.F / 8388608.F, 1e-6F);
}

TEST_F(WavIoTest, StreamingWriterFlushesPlayableHeaderWhileOpen) {
    const auto path = file("live.wav");
    audition::io::StreamingPcm16WavWriter writer(path, 4, 1);
    const std::int16_t first[]{16384, 0, -16384, 0};
    writer.append(first, 4U); // One second automatically flushes the header.
    const auto live = audition::io::loadWav(path);
    ASSERT_EQ(live.audio.samples().size(), 4U);
    EXPECT_FLOAT_EQ(live.audio.samples()[0], 0.5F);

    writer.appendSilence(4U);
    writer.finish();
    const auto completed = audition::io::loadWav(path);
    ASSERT_EQ(completed.audio.samples().size(), 8U);
    EXPECT_FLOAT_EQ(completed.audio.samples()[4], 0.F);
    EXPECT_FLOAT_EQ(completed.audio.samples()[7], 0.F);
}

TEST_F(WavIoTest, StreamingWriterRejectsOversizedCountsWithoutAllocating) {
    audition::io::StreamingPcm16WavWriter writer(file("overflow.wav"), 16000);
    const std::int16_t sample = 1;
    EXPECT_THROW(
        writer.append(&sample, std::numeric_limits<std::size_t>::max()),
        std::overflow_error);
    EXPECT_THROW(
        writer.appendSilence(std::numeric_limits<std::uint64_t>::max()),
        std::overflow_error);
    EXPECT_EQ(writer.sampleCount(), 0U);
    writer.finish();
    EXPECT_EQ(std::filesystem::file_size(file("overflow.wav")), 44U);
}

TEST_F(WavIoTest, StreamingWriterEmptyAndDestructorFinalize) {
    const auto empty = file("empty-stream.wav");
    {
        audition::io::StreamingPcm16WavWriter writer(empty, 16000);
        EXPECT_NO_THROW(writer.append(nullptr, 0U));
        EXPECT_NO_THROW(writer.appendSilence(0U));
        EXPECT_EQ(writer.sampleCount(), 0U);
    }
    EXPECT_EQ(std::filesystem::file_size(empty), 44U);

    const auto path = file("destructor-finalized.wav");
    {
        audition::io::StreamingPcm16WavWriter writer(path, 16000);
        const std::int16_t samples[]{100, -100};
        writer.append(samples, 2U);
    }
    const auto loaded = audition::io::loadWav(path);
    ASSERT_EQ(loaded.audio.samples().size(), 2U);
    EXPECT_NEAR(loaded.audio.samples()[0], 100.F / 32768.F, 1e-6F);
}

TEST_F(WavIoTest, NormalizerRejectsZeroRateAndOddLengthInput) {
    const auto zero_rate = fixture("normalize-no-rate.wav", 1, 16, 1, {0, 0});
    for (std::streamoff offset = 24; offset < 28; ++offset)
        overwriteWavByte(zero_rate, offset, 0);
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        zero_rate, file("normalized-no-rate.wav"))), std::runtime_error);

    const auto odd = fixture("normalize-odd.wav", 1, 16, 1, {0, 0});
    {
        std::ofstream output(odd, std::ios::binary | std::ios::app);
        output.put('X');
    }
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        odd, file("normalized-odd.wav"))), std::runtime_error);
}

TEST_F(WavIoTest, NormalizerRejectsNonfiniteTargetAndExcessiveGain) {
    const auto path = fixture("normalizer-options.wav", 1, 16, 1, {0, 0});
    audition::PeakNormalizeOptions options{};
    options.target_peak_dbfs = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        path, file("nan-target.wav"), options)), std::invalid_argument);
    options = {};
    options.max_gain_db = 121.0;
    EXPECT_THROW(static_cast<void>(audition::io::normalizePcm16Wav(
        path, file("excessive-gain.wav"), options)), std::invalid_argument);
}

TEST_F(WavIoTest, StreamingWriterRejectsInvalidRateAndUnwritableOutput) {
    EXPECT_THROW(
        audition::io::StreamingPcm16WavWriter(file("zero-rate.wav"), 0, 1),
        std::invalid_argument);
    EXPECT_THROW(
        audition::io::StreamingPcm16WavWriter(file("zero-channels.wav"), 16000, 0),
        std::invalid_argument);
    EXPECT_THROW(
        audition::io::StreamingPcm16WavWriter(
            file("huge-rate.wav"), std::numeric_limits<std::uint32_t>::max(), 2),
        std::invalid_argument);
    EXPECT_THROW(
        audition::io::StreamingPcm16WavWriter(
            file("missing-dir") / "bad.wav", 16000),
        std::runtime_error);
}

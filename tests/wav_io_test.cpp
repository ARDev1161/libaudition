#include <audition/io/wav_io.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

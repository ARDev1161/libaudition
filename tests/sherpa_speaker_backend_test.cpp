#include <audition/backends/sherpa.hpp>

#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace {

audition::SpeakerEmbedding embedding(
    std::string model_id,
    std::initializer_list<float> values) {
    return {std::move(model_id), std::vector<float>{values}, std::nullopt};
}

}  // namespace

TEST(SherpaSpeakerConfig, ValidatesEmbeddingOptions) {
    audition::SherpaSpeakerEmbeddingOptions options{};
    EXPECT_THROW(audition::validateSherpaSpeakerEmbeddingOptions(options), audition::Error);

    options.model = "speaker.onnx";
    EXPECT_NO_THROW(audition::validateSherpaSpeakerEmbeddingOptions(options));

    options.sample_rate_hz = 0U;
    EXPECT_THROW(audition::validateSherpaSpeakerEmbeddingOptions(options), audition::Error);
}

TEST(SherpaSpeakerConfig, ValidatesIdentificationIndexOptions) {
    audition::SherpaSpeakerIdentifierOptions options{};
    EXPECT_THROW(audition::validateSherpaSpeakerIdentifierOptions(options), audition::Error);

    options.embedding_dimension = 192U;
    EXPECT_NO_THROW(audition::validateSherpaSpeakerIdentifierOptions(options));
}

TEST(SherpaSpeakerConfig, ValidatesDiarizationOptions) {
    audition::SherpaSpeakerDiarizationOptions options{};
    options.segmentation_model = "segmentation.onnx";
    options.embedding_model = "embedding.onnx";
    EXPECT_NO_THROW(audition::validateSherpaSpeakerDiarizationOptions(options));

    options.segmentation_window_shift_ratio = 0.0F;
    EXPECT_THROW(audition::validateSherpaSpeakerDiarizationOptions(options), audition::Error);
}

TEST(SherpaSpeakerVerifier, ReturnsNativeCosineSimilarity) {
    audition::SherpaSpeakerVerifier verifier{};

    const auto same = verifier.compare(
        embedding("model", {1.0F, 0.0F}),
        embedding("model", {1.0F, 0.0F}),
        audition::Score{0.8});
    EXPECT_TRUE(same.matched);
    EXPECT_NEAR(same.similarity.value, 1.0, 1.0e-6);
    EXPECT_FALSE(same.calibrated_probability.has_value());

    const auto orthogonal = verifier.compare(
        embedding("model", {1.0F, 0.0F}),
        embedding("model", {0.0F, 1.0F}),
        audition::Score{0.8});
    EXPECT_FALSE(orthogonal.matched);
    EXPECT_NEAR(orthogonal.similarity.value, 0.0, 1.0e-6);
}

TEST(SherpaSpeakerVerifier, RejectsModelMismatch) {
    audition::SherpaSpeakerVerifier verifier{};
    EXPECT_THROW(
        verifier.compare(
            embedding("model-a", {1.0F, 0.0F}),
            embedding("model-b", {1.0F, 0.0F}),
            audition::Score{0.5}),
        audition::Error);
}

TEST(SherpaSpeakerIdentifier, EnrollsSearchesAndRemoves) {
    audition::SherpaSpeakerIdentifier identifier{{2U, "model"}};

    audition::SpeakerEnrollment alice{};
    alice.speaker_id = audition::SpeakerId{7U};
    alice.display_name = "Alice";
    alice.embeddings = {
        embedding("model", {1.0F, 0.0F}),
        embedding("model", {0.9F, 0.1F}),
    };
    identifier.enroll(alice);

    audition::SpeakerEnrollment bob{};
    bob.speaker_id = audition::SpeakerId{9U};
    bob.display_name = "Bob";
    bob.embeddings = {embedding("model", {0.0F, 1.0F})};
    identifier.enroll(bob);

    const auto match =
        identifier.identify(embedding("model", {1.0F, 0.0F}), audition::Score{0.5});
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->speaker_id, audition::SpeakerId{7U});
    EXPECT_EQ(match->display_name, "Alice");

    const auto top = identifier.identifyTopK(
        embedding("model", {0.7F, 0.7F}), audition::Score{-1.0}, 2U);
    ASSERT_EQ(top.size(), 2U);

    EXPECT_TRUE(identifier.remove(audition::SpeakerId{7U}));
    EXPECT_FALSE(identifier.remove(audition::SpeakerId{7U}));

    identifier.clear();
    EXPECT_FALSE(identifier.identify(
        embedding("model", {0.0F, 1.0F}), audition::Score{-1.0}).has_value());
}

TEST(SherpaSpeakerIdentifier, RejectsWrongDimensionOrModel) {
    audition::SherpaSpeakerIdentifier identifier{{2U, "model"}};

    audition::SpeakerEnrollment bad_dimension{};
    bad_dimension.speaker_id = audition::SpeakerId{1U};
    bad_dimension.embeddings = {embedding("model", {1.0F})};
    EXPECT_THROW(identifier.enroll(bad_dimension), audition::Error);

    audition::SpeakerEnrollment bad_model{};
    bad_model.speaker_id = audition::SpeakerId{2U};
    bad_model.embeddings = {embedding("other", {1.0F, 0.0F})};
    EXPECT_THROW(identifier.enroll(bad_model), audition::Error);
}

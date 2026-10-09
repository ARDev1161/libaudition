#include <audition/audition.hpp>
#include <audition/interfaces/source_identity.hpp>

#include <gtest/gtest.h>

TEST(PublicHeaders, UmbrellaHeaderCompiles) {
    EXPECT_EQ(audition::kVersionMajor, 0);
    EXPECT_EQ(audition::kVersionMinor, 5);
    EXPECT_EQ(audition::kVersionPatch, 0);
    EXPECT_EQ(audition::kVersion, "0.5.0");
}

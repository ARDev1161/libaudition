#include <acoustic/acoustic.hpp>
#include <acoustic/interfaces/source_identity.hpp>

#include <gtest/gtest.h>

TEST(PublicHeaders, UmbrellaHeaderCompiles) {
    EXPECT_EQ(acoustic::kVersionMajor, 0);
    EXPECT_EQ(acoustic::kVersionMinor, 1);
}

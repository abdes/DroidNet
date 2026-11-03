//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::loose_cooked::RequireIndexRelPath;

//! A path relative to the cooked root, `/`-separated, is listed.
NOLINT_TEST(LooseCookedPathsTest, AcceptsAContainerRelativePath)
{
  EXPECT_NO_THROW(RequireIndexRelPath("Materials/Wood.omat"));
  EXPECT_NO_THROW(RequireIndexRelPath("Resources/textures.data"));
  EXPECT_NO_THROW(RequireIndexRelPath("a.b/c..d/e"));
}

class LooseCookedPathsRefusalTest
  : public ::testing::TestWithParam<std::string_view> { };

//! Every path that breaks a rule is refused.
NOLINT_TEST_P(LooseCookedPathsRefusalTest, RefusesThePath)
{
  EXPECT_THROW(RequireIndexRelPath(GetParam()), std::runtime_error)
    << GetParam();
}

INSTANTIATE_TEST_SUITE_P(BrokenRules, LooseCookedPathsRefusalTest,
  ::testing::Values(std::string_view {}, "container.index.bin",
    ".generation.lock", "container.writer.lock", ".leases", ".leases/3.lock",
    "Materials\\Wood.omat", "C:/Wood.omat", "/Wood.omat", "Materials/",
    "Materials//Wood.omat", "./Wood.omat", "Materials/./Wood", "../Wood.omat",
    "Materials/../Wood.omat", "Materials/..",
    std::string_view { "Materials/a\0b", 13 }, "Materials/\xFF.omat"));

//! The root's own files are reserved; a name that only starts like the
//! leases folder is not.
NOLINT_TEST(LooseCookedPathsTest, ReservesTheRootsOwnFiles)
{
  using oxygen::data::loose_cooked::IsReservedRelPath;

  EXPECT_TRUE(IsReservedRelPath("container.index.bin"));
  EXPECT_TRUE(IsReservedRelPath("container.writer.lock"));
  EXPECT_TRUE(IsReservedRelPath(".generation.lock"));
  EXPECT_TRUE(IsReservedRelPath(".leases"));
  EXPECT_TRUE(IsReservedRelPath(".leases/12.lock"));
  EXPECT_FALSE(IsReservedRelPath(".leasesX/12.lock"));
  EXPECT_FALSE(IsReservedRelPath("Materials/container.index.bin"));
}

//! A reader lease names its commit in decimal, and only such a name is one.
NOLINT_TEST(LooseCookedPathsTest, LeaseNamesItsCommit)
{
  using oxygen::data::loose_cooked::LeasedCommitOf;
  using oxygen::data::loose_cooked::LeaseRelPath;

  EXPECT_EQ(LeaseRelPath(42), ".leases/42.lock");
  EXPECT_EQ(LeasedCommitOf("42.lock"), std::optional<uint64_t> { 42 });
  EXPECT_EQ(LeasedCommitOf(".lock"), std::nullopt);
  EXPECT_EQ(LeasedCommitOf("4x.lock"), std::nullopt);
  EXPECT_EQ(LeasedCommitOf("42.tmp"), std::nullopt);
}

//! A content name puts 16 hexadecimal digits of the digest before the
//! extension, and only such a name has the form.
NOLINT_TEST(LooseCookedPathsTest, ContentNameCarriesTheDigest)
{
  using oxygen::data::loose_cooked::ContentRelPath;
  using oxygen::data::loose_cooked::IsContentRelPath;

  auto digest = oxygen::base::Sha256Digest {};
  digest.at(0) = 0x3F;
  digest.at(7) = 0xA8;

  const auto named = ContentRelPath("Materials/Red.omat", digest);
  const auto bare = ContentRelPath("Resources/LICENSE", digest);

  EXPECT_EQ(named, "Materials/Red.3f000000000000a8.omat");
  EXPECT_EQ(bare, "Resources/LICENSE.3f000000000000a8");
  EXPECT_TRUE(IsContentRelPath(named));
  EXPECT_TRUE(IsContentRelPath(bare));
  EXPECT_FALSE(IsContentRelPath("Materials/Red.omat"));
  EXPECT_FALSE(IsContentRelPath("Materials/Red.3F000000000000A8.omat"));
  EXPECT_FALSE(IsContentRelPath("Materials/Red.3f00.omat"));
  EXPECT_FALSE(IsContentRelPath(".3f000000000000a8"));
}

//! What only a writer produces is a content name or an atomic sibling.
NOLINT_TEST(LooseCookedPathsTest, WriterOutputIsContentNamedOrASibling)
{
  using oxygen::data::loose_cooked::IsWriterOutputRelPath;

  EXPECT_TRUE(IsWriterOutputRelPath("Materials/Red.3f000000000000a8.omat"));
  EXPECT_TRUE(IsWriterOutputRelPath(
    "Materials/.oxygen-0190f3a2-5c1b-7d2e-8f3a-1b2c3d4e5f60.tmp"));
  EXPECT_FALSE(IsWriterOutputRelPath("Materials/Red.omat"));
  EXPECT_FALSE(IsWriterOutputRelPath("Materials/.oxygen-not-a-uuid.tmp"));
}

} // namespace

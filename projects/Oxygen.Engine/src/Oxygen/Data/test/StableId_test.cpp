//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include <fmt/format.h>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::NodeId;
using oxygen::data::ScriptSlotId;
using oxygen::data::SurfaceId;
using oxygen::data::to_string;

static_assert(!std::is_convertible_v<SurfaceId, oxygen::data::AssetKey>);
static_assert(!std::is_convertible_v<oxygen::data::AssetKey, SurfaceId>);
// Each kind of ID is its own type: one cannot stand in for another.
static_assert(!std::is_convertible_v<NodeId, SurfaceId>);
static_assert(!std::is_convertible_v<SurfaceId, NodeId>);
static_assert(!std::is_convertible_v<NodeId, ScriptSlotId>);
static_assert(!std::is_convertible_v<ScriptSlotId, NodeId>);
static_assert(!std::is_convertible_v<ScriptSlotId, SurfaceId>);

NOLINT_TEST(StableIdTest, PreservesAllOpaqueBytesAndCanonicalText)
{
  constexpr SurfaceId::ByteArray kBytes {
    0x01,
    0x23,
    0x45,
    0x67,
    0x89,
    0xab,
    0xcd,
    0xef,
    0xfe,
    0xdc,
    0xba,
    0x98,
    0x76,
    0x54,
    0x32,
    0x10,
  };
  constexpr std::string_view kText = "01234567-89ab-cdef-fedc-ba9876543210";
  constexpr auto kId = SurfaceId::FromBytes(kBytes);
  EXPECT_FALSE(kId.IsNil());
  EXPECT_EQ(sizeof(kId), 16U);
  EXPECT_EQ(to_string(kId), kText);
  EXPECT_EQ(fmt::format("{}", kId), kText);
  const auto parsed = SurfaceId::FromString(kText);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed.value(), kId);
  EXPECT_EQ(oxygen::data::as_bytes(kId).size(), 16U);
  EXPECT_EQ(oxygen::data::as_bytes(kId).front(), std::byte { 0x01 });
  EXPECT_EQ(oxygen::data::as_bytes(kId).back(), std::byte { 0x10 });
}

NOLINT_TEST(StableIdTest, RejectsNoncanonicalTextWithoutNormalization)
{
  constexpr std::array<std::string_view, 12> kInvalid {
    "",
    "0123456789abcdef-fedc-ba9876543210",
    "01234567-89ab-cdef-fedc-ba987654321",
    "01234567-89ab-cdef-fedc-ba98765432100",
    "01234567-89AB-cdef-fedc-ba9876543210",
    "01234567-89ab-cdef-fedc-ba987654321G",
    "{01234567-89ab-cdef-fedc-ba9876543210}",
    " 01234567-89ab-cdef-fedc-ba9876543210",
    "01234567-89ab-cdef-fedc-ba9876543210 ",
    "01234567_89ab-cdef-fedc-ba9876543210",
    "01234567--9ab-cdef-fedc-ba9876543210",
    "urn:uuid:01234567-89ab-cdef-fedc-ba9876543210",
  };
  for (const auto text : kInvalid) {
    SCOPED_TRACE(text);
    EXPECT_FALSE(SurfaceId::FromString(text).has_value());
  }
}

NOLINT_TEST(StableIdTest, NilIsAnUnsetSentinelAndOrderingUsesAllBytes)
{
  EXPECT_TRUE(SurfaceId {}.IsNil());
  const auto nil
    = SurfaceId::FromString("00000000-0000-0000-0000-000000000000");
  ASSERT_TRUE(nil.has_value());
  EXPECT_TRUE(nil.value().IsNil());
  constexpr auto first = [] -> SurfaceId {
    SurfaceId::ByteArray bytes {};
    bytes.front() = 1;
    return SurfaceId::FromBytes(bytes);
  }();
  constexpr auto second = [] -> SurfaceId {
    SurfaceId::ByteArray bytes {};
    bytes.front() = 1;
    bytes.back() = 1;
    return SurfaceId::FromBytes(bytes);
  }();
  EXPECT_LT(first, second);
  EXPECT_NE(first, second);
  const std::unordered_set<SurfaceId> unique { first, first, second };
  EXPECT_EQ(unique.size(), 2U);
  EXPECT_TRUE(unique.contains(first));
  EXPECT_TRUE(unique.contains(second));
}

//! An ID derived from a canonical identity is the same on every derivation
//! and differs for another identity; an empty identity is refused, named by
//! the ID's kind.
NOLINT_TEST(StableIdTest, DerivesTheSameIdFromTheSameIdentity)
{
  const auto first = NodeId::FromStableIdentity("/model/Body/Door_FL");
  const auto again = NodeId::FromStableIdentity("/model/Body/Door_FL");
  const auto other = NodeId::FromStableIdentity("/model/Body/Door_FR");

  EXPECT_FALSE(first.IsNil());
  EXPECT_EQ(first, again);
  EXPECT_NE(first, other);
  NOLINT_EXPECT_THROW(
    static_cast<void>(NodeId::FromStableIdentity("")), std::invalid_argument);
  try {
    static_cast<void>(ScriptSlotId::FromStableIdentity(""));
    ADD_FAILURE() << "an empty identity derived an ID";
  } catch (const std::invalid_argument& error) {
    EXPECT_EQ(std::string_view { error.what() },
      "Script slot identity must not be empty");
  }
}

//! A generated ID is never nil and never repeats.
NOLINT_TEST(StableIdTest, GeneratesDistinctIds)
{
  const auto first = NodeId::Generate();
  const auto second = NodeId::Generate();

  EXPECT_FALSE(first.IsNil());
  EXPECT_NE(first, second);
}

} // namespace

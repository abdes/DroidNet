//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::MaterialSlotId;
using oxygen::data::to_string;

static_assert(!std::is_convertible_v<MaterialSlotId, oxygen::data::AssetKey>);
static_assert(!std::is_convertible_v<oxygen::data::AssetKey, MaterialSlotId>);

NOLINT_TEST(MaterialSlotIdTest, PreservesAllOpaqueBytesAndCanonicalText)
{
  constexpr MaterialSlotId::ByteArray kBytes { 0x01, 0x23, 0x45, 0x67, 0x89,
    0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 };
  constexpr std::string_view kText = "01234567-89ab-cdef-fedc-ba9876543210";
  constexpr auto kId = MaterialSlotId::FromBytes(kBytes);
  EXPECT_FALSE(kId.IsNil());
  EXPECT_EQ(sizeof(kId), 16U);
  EXPECT_EQ(to_string(kId), kText);
  EXPECT_EQ(fmt::format("{}", kId), kText);
  const auto parsed = MaterialSlotId::FromString(kText);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed.value(), kId);
  EXPECT_EQ(oxygen::data::as_bytes(kId).size(), 16U);
  EXPECT_EQ(oxygen::data::as_bytes(kId).front(), std::byte { 0x01 });
  EXPECT_EQ(oxygen::data::as_bytes(kId).back(), std::byte { 0x10 });
}

NOLINT_TEST(MaterialSlotIdTest, RejectsNoncanonicalTextWithoutNormalization)
{
  constexpr std::array<std::string_view, 12> kInvalid { "",
    "0123456789abcdef-fedc-ba9876543210", "01234567-89ab-cdef-fedc-ba987654321",
    "01234567-89ab-cdef-fedc-ba98765432100",
    "01234567-89AB-cdef-fedc-ba9876543210",
    "01234567-89ab-cdef-fedc-ba987654321G",
    "{01234567-89ab-cdef-fedc-ba9876543210}",
    " 01234567-89ab-cdef-fedc-ba9876543210",
    "01234567-89ab-cdef-fedc-ba9876543210 ",
    "01234567_89ab-cdef-fedc-ba9876543210",
    "01234567--9ab-cdef-fedc-ba9876543210",
    "urn:uuid:01234567-89ab-cdef-fedc-ba9876543210" };
  for (const auto text : kInvalid) {
    SCOPED_TRACE(text);
    EXPECT_FALSE(MaterialSlotId::FromString(text).has_value());
  }
}

NOLINT_TEST(MaterialSlotIdTest, NilIsAnUnsetSentinelAndOrderingUsesAllBytes)
{
  EXPECT_TRUE(MaterialSlotId {}.IsNil());
  const auto nil
    = MaterialSlotId::FromString("00000000-0000-0000-0000-000000000000");
  ASSERT_TRUE(nil.has_value());
  EXPECT_TRUE(nil.value().IsNil());
  constexpr auto first = [] {
    MaterialSlotId::ByteArray bytes {};
    bytes.front() = 1;
    return MaterialSlotId::FromBytes(bytes);
  }();
  constexpr auto second = [] {
    MaterialSlotId::ByteArray bytes {};
    bytes.front() = 1;
    bytes.back() = 1;
    return MaterialSlotId::FromBytes(bytes);
  }();
  EXPECT_LT(first, second);
  EXPECT_NE(first, second);
  const std::unordered_set<MaterialSlotId> unique { first, first, second };
  EXPECT_EQ(unique.size(), 2U);
  EXPECT_TRUE(unique.contains(first));
  EXPECT_TRUE(unique.contains(second));
}

} // namespace

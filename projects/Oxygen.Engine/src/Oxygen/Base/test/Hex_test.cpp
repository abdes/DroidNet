//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Base/Hex.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using oxygen::base::FromHexString;
using oxygen::base::ToHexString;
using testing::ElementsAreArray;

constexpr auto kEdgeBytes = std::array {
  std::byte { 0x00 },
  std::byte { 0x0F },
  std::byte { 0x10 },
  std::byte { 0xA5 },
  std::byte { 0xFF },
};
constexpr auto kEdgeHex = std::string_view { "000f10a5ff" };

//! Bytes are spelled as two lowercase digits each, first byte first.
NOLINT_TEST(HexTest, ToHexStringSpellsLowercaseDigitPairs)
{
  EXPECT_EQ(ToHexString(kEdgeBytes), kEdgeHex);
  EXPECT_EQ(ToHexString(std::span<const std::byte> {}), "");
}

//! Reading a spelling gives the bytes back, whatever the digit case.
NOLINT_TEST(HexTest, FromHexStringReadsEitherCase)
{
  auto lower = std::array<std::byte, kEdgeBytes.size()> {};
  auto upper = std::array<std::byte, kEdgeBytes.size()> {};

  EXPECT_TRUE(FromHexString(kEdgeHex, lower));
  EXPECT_TRUE(FromHexString("000F10A5fF", upper));

  EXPECT_THAT(lower, ElementsAreArray(kEdgeBytes));
  EXPECT_THAT(upper, ElementsAreArray(kEdgeBytes));
  EXPECT_EQ(ToHexString(lower), kEdgeHex);
}

//! Every byte value survives the round trip.
NOLINT_TEST(HexTest, EveryByteValueRoundTrips)
{
  constexpr auto kByteValues = size_t { 256U };
  auto bytes = std::array<std::byte, kByteValues> {};
  for (auto index = size_t { 0U }; index < bytes.size(); ++index) {
    bytes.at(index) = static_cast<std::byte>(index);
  }
  auto read = std::array<std::byte, kByteValues> {};

  EXPECT_TRUE(FromHexString(ToHexString(bytes), read));

  EXPECT_THAT(read, ElementsAreArray(bytes));
}

//! Text that is not exactly two digits per output byte is refused.
NOLINT_TEST(HexTest, FromHexStringRefusesAnyOtherText)
{
  const auto valid = std::string { kEdgeHex };
  const auto with_pair = [&valid](const std::string_view pair) -> std::string {
    return std::string { pair } + valid.substr(2U);
  };
  const auto rejected = std::array {
    std::string {},
    valid.substr(1U),
    valid.substr(2U),
    valid + "0",
    valid + "00",
    with_pair("g0"),
    with_pair("0G"),
    with_pair("-1"),
    with_pair("+1"),
    with_pair(" a"),
    with_pair("0x"),
    with_pair(std::string_view { "\0a", 2U }),
  };

  for (const auto& text : rejected) {
    auto out = std::array<std::byte, kEdgeBytes.size()> {};
    EXPECT_FALSE(FromHexString(text, out)) << "accepted '" << text << "'";
  }
}

//! Nothing reads into nothing, and only the empty text does.
NOLINT_TEST(HexTest, EmptyOutputTakesOnlyEmptyText)
{
  EXPECT_TRUE(FromHexString("", {}));
  EXPECT_FALSE(FromHexString("00", {}));
}

//! The SHA-256 and UUID spellings built on the codec are unchanged.
NOLINT_TEST(HexTest, DigestAndUuidSpellingsAreUnchanged)
{
  constexpr auto kAbcDigest = std::string_view {
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
  };
  constexpr auto kUuid
    = std::string_view { "0190a3b4-c5d6-7e8f-9a0b-1c2d3e4f5a6b" };

  const auto digest = oxygen::base::ComputeSha256(
    std::as_bytes(std::span { std::string_view { "abc" } }));
  const auto uuid = oxygen::Uuid::FromString(kUuid);

  EXPECT_EQ(oxygen::base::ToHexString(digest), kAbcDigest);
  ASSERT_TRUE(uuid.has_value());
  EXPECT_EQ(uuid.value().ToString(), kUuid);
  EXPECT_FALSE(oxygen::Uuid::FromString("0190A3B4-C5D6-7E8F-9A0B-1C2D3E4F5A6B")
      .has_value());
}

} // namespace

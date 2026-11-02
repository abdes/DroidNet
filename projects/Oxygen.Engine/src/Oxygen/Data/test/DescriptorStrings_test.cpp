//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::DescriptorStrings;
using oxygen::data::DescriptorStringsBuilder;

//! A built table resolves each string at the offset it was given, and a
//! builder continuing the table keeps every offset.
NOLINT_TEST(DescriptorStringsTest, BuiltTableResolvesItsStrings)
{
  auto builder = DescriptorStringsBuilder {};
  const auto wood = builder.Add("Wood");
  const auto accent = builder.Add("Caf\xC3\xA9");
  const auto bytes = builder.Bytes();
  auto chars = std::vector<char>(bytes.size());
  std::ranges::transform(bytes, chars.begin(),
    [](const std::byte value) -> char { return static_cast<char>(value); });

  const auto strings = DescriptorStrings { std::move(chars) };
  auto continued = DescriptorStringsBuilder { strings };

  EXPECT_EQ(strings.Get(0), std::string_view {});
  EXPECT_EQ(strings.Get(wood), "Wood");
  EXPECT_EQ(strings.Get(accent), "Caf\xC3\xA9");
  EXPECT_EQ(continued.Add("Wood"), wood);
  EXPECT_EQ(continued.Add("Caf\xC3\xA9"), accent);
}

//! Only an offset where a string starts names one: an offset into a string,
//! even one that splits a UTF-8 character, resolves nothing.
NOLINT_TEST(DescriptorStringsTest, ResolvesOnlyOffsetsWhereAStringStarts)
{
  auto builder = DescriptorStringsBuilder {};
  const auto accent = builder.Add("Caf\xC3\xA9");
  const auto strings = builder.Build();
  // The byte after `\xC3` continues the character it starts.
  const auto into_the_character = accent + 4U;
  const auto into_the_string = accent + 1U;

  EXPECT_FALSE(strings.TryGet(into_the_character).has_value());
  EXPECT_FALSE(strings.TryGet(into_the_string).has_value());
  EXPECT_THROW(
    static_cast<void>(strings.Get(into_the_character)), std::invalid_argument);
  const auto whole = strings.TryGet(accent);
  ASSERT_HAS_VALUE(whole);
  EXPECT_EQ(*whole, "Caf\xC3\xA9");
}

//! A terminated table whose bytes are not UTF-8 is refused.
NOLINT_TEST(DescriptorStringsTest, RefusesATableThatIsNotUtf8)
{
  EXPECT_THROW(DescriptorStrings(std::vector<char> { '\0', '\xFF', '\0' }),
    std::invalid_argument);
}

//! A string that is not UTF-8, or holds a NUL, is never added.
NOLINT_TEST(DescriptorStringsTest, BuilderRefusesStringsItCannotStore)
{
  auto builder = DescriptorStringsBuilder {};

  EXPECT_THROW(
    static_cast<void>(builder.Add("Caf\xC3")), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(builder.Add(std::string_view { "a\0b", 3 })),
    std::invalid_argument);
  EXPECT_EQ(builder.Size(), 1U);
}

} // namespace

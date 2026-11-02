//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include <gmock/gmock-matchers.h>

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Base/Windows/Exceptions.h>
#include <Oxygen/Testing/GTest.h>

#ifdef OXYGEN_WINDOWS
using oxygen::windows::WindowsException;
#endif

namespace oxygen::string_utils {

struct StringConversionTestParam {
  std::variant<const char*, const char8_t*, const uint8_t*, std::string_view,
    std::u8string_view>
    input;
  std::wstring expected_output;
};

class ToWideTest : public ::testing::TestWithParam<StringConversionTestParam> {
};

NOLINT_TEST_P(ToWideTest, ConvertsValidUtf8SequenceToWideString)
{
  const auto& [input, expected_output] = GetParam();
  std::wstring output;

  NOLINT_EXPECT_NO_THROW(
    std::visit([&](auto&& arg) { Utf8ToWide(arg, output); }, input););

  EXPECT_EQ(output, expected_output);
}

INSTANTIATE_TEST_SUITE_P( // NOLINT
  ToWideTests, ToWideTest,
  ::testing::Values(StringConversionTestParam { std::string_view(""), L"" },
    StringConversionTestParam { std::u8string_view(u8""), L"" },
    StringConversionTestParam { "", L"" },
    StringConversionTestParam { u8"", L"" },
    StringConversionTestParam { reinterpret_cast<const uint8_t*>(""), L"" },
    StringConversionTestParam {
      std::string_view("Hello, World!"), L"Hello, World!" },
    StringConversionTestParam {
      std::u8string_view(u8"Hello, World!"), L"Hello, World!" },
    StringConversionTestParam { "Hello, World!", L"Hello, World!" },
    StringConversionTestParam { u8"Hello, World!", L"Hello, World!" },
    StringConversionTestParam {
      reinterpret_cast<const uint8_t*>("Hello, World!"), L"Hello, World!" },
    StringConversionTestParam {
      std::string_view("こんにちは世界"), L"こんにちは世界" },
    StringConversionTestParam {
      std::u8string_view(u8"こんにちは世界"), L"こんにちは世界" },
    StringConversionTestParam { "こんにちは世界", L"こんにちは世界" },
    StringConversionTestParam { u8"こんにちは世界", L"こんにちは世界" },
    StringConversionTestParam {
      reinterpret_cast<const uint8_t*>("こんにちは世界"), L"こんにちは世界" }),
  [](const ::testing::TestParamInfo<ToWideTest::ParamType>& info) {
    // Return a simple name based on the index
    return std::to_string(info.index);
  });

NOLINT_TEST_F(ToWideTest, RejectsInvalidUtf8Sequence)
{
  constexpr char invalid_utf8[]
    = { '\xC3', '\x28', '\0' }; // 0xC3 0x28 is invalid UTF-8
  std::wstring output {};
  try {
    Utf8ToWide(&invalid_utf8[0], output);
    FAIL() << "Expected an invalid-sequence exception";
#ifdef OXYGEN_WINDOWS
  } catch (const WindowsException& ex) {
    static_assert(ERROR_NO_UNICODE_TRANSLATION >= 0);
    EXPECT_EQ(ex.GetErrorCode(),
      static_cast<DWORD>(
        ERROR_NO_UNICODE_TRANSLATION)); // Replace with the expected error code
    EXPECT_THAT(ex.what(), ::testing::StartsWith("1113"));
#else
  } catch (const std::runtime_error& ex) {
    EXPECT_THAT(ex.what(), ::testing::HasSubstr("Invalid"));
#endif
  } catch (...) {
    FAIL() << "Expected an invalid-sequence exception";
  }

  EXPECT_TRUE(output.empty());
}

NOLINT_TEST_F(ToWideTest, CanConvertLargeUtf8String)
{
  constexpr size_t length = 1000;
  const std::u8string big_utf8(length, u8'*');

  std::wstring output {};
  NOLINT_EXPECT_NO_THROW(Utf8ToWide(big_utf8, output));

  EXPECT_EQ(length, output.size());
}

struct WideStringConversionTestParam {
  std::variant<const wchar_t*, const char16_t*, std::wstring_view, std::wstring>
    input;
  std::variant<const char*, const char8_t*> expected_output;
};

class ToUtf8Test
  : public ::testing::TestWithParam<WideStringConversionTestParam> { };

NOLINT_TEST_P(ToUtf8Test, ConvertsValidWideStringToUtf8String)
{
  const auto& [input, expected_output] = GetParam();
  std::string output;

  NOLINT_EXPECT_NO_THROW(
    std::visit([&](auto&& arg) { WideToUtf8(arg, output); }, input););

  std::string expected;
  std::visit(
    [&](auto&& arg) -> auto {
      const auto text = std::basic_string_view(arg);
      expected.assign(text.begin(), text.end());
    },
    expected_output);

  EXPECT_EQ(output, expected);
}

INSTANTIATE_TEST_SUITE_P( // NOLINT
  ToUtf8Tests, ToUtf8Test,
  ::testing::Values(WideStringConversionTestParam { L"", u8"" },
    WideStringConversionTestParam { L"Hello, World!", u8"Hello, World!" },
    WideStringConversionTestParam { L"こんにちは世界", u8"こんにちは世界" },
    WideStringConversionTestParam {
      std::wstring_view(L"Hello, World!"), u8"Hello, World!" },
    WideStringConversionTestParam {
      std::wstring_view(L"こんにちは世界"), u8"こんにちは世界" },
    WideStringConversionTestParam {
      std::wstring(L"Hello, World!"), u8"Hello, World!" },
    WideStringConversionTestParam {
      std::wstring(L"こんにちは世界"), u8"こんにちは世界" },
    WideStringConversionTestParam { u"Hello, World!", u8"Hello, World!" },
    WideStringConversionTestParam { u"こんにちは世界", u8"こんにちは世界" }),
  [](const ::testing::TestParamInfo<ToUtf8Test::ParamType>& info) {
    // Return a simple name based on the index
    return std::to_string(info.index);
  });

NOLINT_TEST_F(ToUtf8Test, RejectsInvalidWideSequence)
{
  constexpr wchar_t invalid_wide[] = { 0xD800, L'a', L'\0' };
  std::string output {};
  try {
    WideToUtf8(&invalid_wide[0], output);
    FAIL() << "Expected an invalid-sequence exception";
#ifdef OXYGEN_WINDOWS
  } catch (const WindowsException& ex) {
    static_assert(ERROR_NO_UNICODE_TRANSLATION >= 0);
    EXPECT_EQ(
      ex.GetErrorCode(), static_cast<DWORD>(ERROR_NO_UNICODE_TRANSLATION));
    EXPECT_THAT(ex.what(), ::testing::StartsWith("1113"));
#else
  } catch (const std::runtime_error& ex) {
    EXPECT_THAT(ex.what(), ::testing::HasSubstr("Invalid"));
#endif
  } catch (...) {
    FAIL() << "Expected an invalid-sequence exception";
  }
  EXPECT_TRUE(output.empty());
}

NOLINT_TEST_F(ToUtf8Test, CanConvertLargeWideString)
{
  constexpr size_t length = 200;
  constexpr wchar_t value = L'a';
  // Create an array and fill it with the same value
  std::array<wchar_t, length> big_wide {};
  big_wide.fill(value);
  big_wide.back() = L'\0'; // Null-terminate the string

  std::string output {};
  NOLINT_EXPECT_NO_THROW(WideToUtf8(big_wide.data(), output););

  EXPECT_EQ(length - 1, output.size());
}

//! A length the Windows conversion APIs take as an `int` is passed as is up
//! to the largest one; a longer one is refused, never narrowed to a negative
//! length that would make the API scan for a terminator.
NOLINT_TEST(ApiLengthTest, RefusesALengthAnIntCannotHold)
{
  constexpr auto kLargest = std::numeric_limits<int>::max();

  EXPECT_EQ(detail::ToApiLength(0U), 0);
  EXPECT_EQ(detail::ToApiLength(static_cast<size_t>(kLargest)), kLargest);
  EXPECT_THROW(
    static_cast<void>(detail::ToApiLength(static_cast<size_t>(kLargest) + 1U)),
    std::length_error);
  EXPECT_THROW(
    static_cast<void>(detail::ToApiLength(std::numeric_limits<size_t>::max())),
    std::length_error);
}

//=== UTF-16 (char16_t) to UTF-8 Conversion Tests ===-------------------------//

namespace {
  //! UTF-16 code units: 'G' 'r' 'ü' U+1F600 (surrogate pair) 'Ω',
  //! NUL-terminated.
  constexpr std::array<char16_t, 7> kUtf16Text {
    char16_t { 'G' },
    char16_t { 'r' },
    char16_t { 0x00FC },
    char16_t { 0xD83D },
    char16_t { 0xDE00 },
    char16_t { 0x03A9 },
    char16_t { 0 },
  };

  //! The same text encoded in UTF-8: G r C3BC F09F9880 CEA9.
  constexpr std::string_view kExpectedUtf8 {
    "G\x72\xC3\xBC\xF0\x9F\x98\x80\xCE\xA9"
  };
} // namespace

//! char16_t input converts correctly on every platform (on Linux wchar_t is
//! UTF-32, so the storage must never be reinterpreted).
NOLINT_TEST(StringUtilsTest, WideToUtf8_Char16IntoNarrowString)
{
  std::string output;
  NOLINT_EXPECT_NO_THROW(WideToUtf8(kUtf16Text.data(), output));
  EXPECT_EQ(output, kExpectedUtf8);
}

NOLINT_TEST(StringUtilsTest, WideToUtf8_Char16IntoUtf8String)
{
  std::u8string output;
  NOLINT_EXPECT_NO_THROW(WideToUtf8(kUtf16Text.data(), output));
  const std::string as_narrow(output.begin(), output.end());
  EXPECT_EQ(as_narrow, kExpectedUtf8);
}

NOLINT_TEST(StringUtilsTest, WideToUtf8_Char16LoneSurrogateThrows)
{
  constexpr std::array<char16_t, 3> lone_surrogate {
    char16_t { 0xD83D },
    char16_t { 'x' },
    char16_t { 0 },
  };
  std::string output;
  NOLINT_EXPECT_THROW(
    WideToUtf8(lone_surrogate.data(), output), std::exception);
}

//! Strict UTF-8 accepts every well-formed sequence length, the empty text and
//! embedded NUL, which is a valid code point.
NOLINT_TEST(StringUtilsTest, IsValidUtf8AcceptsWellFormedText)
{
  EXPECT_TRUE(IsValidUtf8(""));
  EXPECT_TRUE(IsValidUtf8(kExpectedUtf8));
  EXPECT_TRUE(IsValidUtf8(std::string_view { "a\0b", 3 }));
  EXPECT_TRUE(IsValidUtf8("\xF4\x8F\xBF\xBF")); // U+10FFFF
}

//! Strict UTF-8 refuses every malformed sequence the decoder refuses.
NOLINT_TEST(StringUtilsTest, IsValidUtf8RefusesMalformedText)
{
  EXPECT_FALSE(IsValidUtf8("\xFF")); // no lead byte
  EXPECT_FALSE(IsValidUtf8("\x80")); // lone continuation
  EXPECT_FALSE(IsValidUtf8("\xC3")); // truncated
  EXPECT_FALSE(IsValidUtf8("\xC3\x28")); // bad continuation
  EXPECT_FALSE(IsValidUtf8("\xC0\xAF")); // overlong '/'
  EXPECT_FALSE(IsValidUtf8("\xED\xA0\x80")); // surrogate U+D800
  EXPECT_FALSE(IsValidUtf8("\xF4\x90\x80\x80")); // above U+10FFFF
}

} // namespace oxygen::string_utils

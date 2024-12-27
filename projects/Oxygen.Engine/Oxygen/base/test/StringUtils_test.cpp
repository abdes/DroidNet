//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/Platforms.h"
#if defined(OXYGEN_WINDOWS)

#include "Oxygen/Base/StringUtils.h"

#include <array>
#include <string_view>
#include <variant>

#include <winerror.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using oxygen::windows::WindowsException;

namespace oxygen::string_utils {

  struct StringConversionTestParam
  {
    std::variant<const char*, const char8_t*, const uint8_t*, std::string_view, std::u8string_view> input;
    std::wstring expected_output;
  };

  class ToWideTest : public ::testing::TestWithParam<StringConversionTestParam> {};

  TEST_P(ToWideTest, ConvertsValidUtf8SequenceToWideString) {
    const auto& [input, expected_output] = GetParam();
    std::wstring output;

    EXPECT_NO_THROW(
      std::visit([&](auto&& arg)
                 {
                   Utf8ToWide(arg, output);
                 }, input);
    );

    EXPECT_EQ(output, expected_output);
  }

  INSTANTIATE_TEST_SUITE_P(
    ToWideTests,
    ToWideTest,
    ::testing::Values(
      StringConversionTestParam{ std::string_view(""), L"" },
      StringConversionTestParam{ std::u8string_view(u8""), L"" },
      StringConversionTestParam{ "", L"" },
      StringConversionTestParam{ u8"", L"" },
      StringConversionTestParam{ reinterpret_cast<const uint8_t*>(""), L"" },
      StringConversionTestParam{ std::string_view("Hello, World!"), L"Hello, World!" },
      StringConversionTestParam{ std::u8string_view(u8"Hello, World!"), L"Hello, World!" },
      StringConversionTestParam{ "Hello, World!", L"Hello, World!" },
      StringConversionTestParam{ u8"Hello, World!", L"Hello, World!" },
      StringConversionTestParam{ reinterpret_cast<const uint8_t*>("Hello, World!"), L"Hello, World!" },
      StringConversionTestParam{ std::string_view("こんにちは世界"), L"こんにちは世界" },
      StringConversionTestParam{ std::u8string_view(u8"こんにちは世界"), L"こんにちは世界" },
      StringConversionTestParam{ "こんにちは世界", L"こんにちは世界" },
      StringConversionTestParam{ u8"こんにちは世界", L"こんにちは世界" },
      StringConversionTestParam{ reinterpret_cast<const uint8_t*>("こんにちは世界"), L"こんにちは世界" }
    )
  );

  TEST_F(ToWideTest, RejectsInvalidUtf8Sequence)
  {
    constexpr char invalid_utf8[] = { '\xC3', '\x28', '\0' }; // 0xC3 0x28 is invalid UTF-8
    std::wstring output{};
    try {
      Utf8ToWide(invalid_utf8, output);
      FAIL() << "Expected oxygen::windows::WindowsException";
    }
    catch (const WindowsException& ex) {
      EXPECT_EQ(ex.GetErrorCode(), ERROR_NO_UNICODE_TRANSLATION); // Replace with the expected error code
      EXPECT_THAT(ex.what(), testing::StartsWith("1113"));
    }
    catch (...) {
      FAIL() << "Expected oxygen::windows::WindowsException";
    }

    EXPECT_TRUE(output.empty());
  }

  TEST_F(ToWideTest, CanConvertLargeUtf8String)
  {
    constexpr size_t length = 1000;
    constexpr char8_t value = 42;
    // Create an array and fill it with the same value
    std::array<char8_t, length> big_utf8;
    big_utf8.fill(value);
    big_utf8.back() = '\0'; // Null-terminate the string

    std::wstring output{};
    const std::string_view view(reinterpret_cast<const char*>(big_utf8.data()));
    EXPECT_NO_THROW(Utf8ToWide(view, output));

    EXPECT_EQ(length - 1, output.size());
  }

  struct WideStringConversionTestParam
  {
    std::variant<const wchar_t*, const char16_t*, std::wstring_view, std::wstring> input;
    std::variant<const char*, const char8_t*> expected_output;
  };

  class ToUtf8Test : public ::testing::TestWithParam<WideStringConversionTestParam> {};

  TEST_P(ToUtf8Test, ConvertsValidWideStringToUtf8String) {
    const auto& [input, expected_output] = GetParam();
    std::string output;

    EXPECT_NO_THROW(
      std::visit([&](auto&& arg)
                 {
                   WideToUtf8(arg, output);
                 }, input);
    );

    std::string expected;
    std::visit([&](auto&& arg)
               {
                 expected = std::string(reinterpret_cast<const char*>(arg));
               }, expected_output);

    EXPECT_EQ(output, expected);
  }

  INSTANTIATE_TEST_SUITE_P(
    ToUtf8Tests,
    ToUtf8Test,
    ::testing::Values(
      WideStringConversionTestParam{ L"", u8"" },
      WideStringConversionTestParam{ L"Hello, World!", u8"Hello, World!" },
      WideStringConversionTestParam{ L"こんにちは世界", u8"こんにちは世界" },
      WideStringConversionTestParam{ std::wstring_view(L"Hello, World!"), u8"Hello, World!" },
      WideStringConversionTestParam{ std::wstring_view(L"こんにちは世界"), u8"こんにちは世界" },
      WideStringConversionTestParam{ std::wstring(L"Hello, World!"), u8"Hello, World!" },
      WideStringConversionTestParam{ std::wstring(L"こんにちは世界"), u8"こんにちは世界" },
      WideStringConversionTestParam{ reinterpret_cast<const char16_t*>(L"Hello, World!"), u8"Hello, World!" },
      WideStringConversionTestParam{ reinterpret_cast<const char16_t*>(L"こんにちは世界"), u8"こんにちは世界" }
    )
  );

  TEST_F(ToUtf8Test, RejectsInvalidWideSequence)
  {
    constexpr wchar_t invalid_wide[] = { 0xD800, L'a', L'\0' };
    std::string output{};
    try {
      WideToUtf8(invalid_wide, output);
      FAIL() << "Expected oxygen::windows::WindowsException";
    }
    catch (const WindowsException& ex) {
      EXPECT_EQ(ex.GetErrorCode(), ERROR_NO_UNICODE_TRANSLATION); // Replace with the expected error code
      EXPECT_THAT(ex.what(), testing::StartsWith("1113"));
    }
    catch (...) {
      FAIL() << "Expected oxygen::windows::WindowsException";
    }
    EXPECT_TRUE(output.empty());
  }

  TEST_F(ToUtf8Test, CanConvertLargeWideString)
  {
    constexpr size_t length = 200;
    constexpr wchar_t value = L'a';
    // Create an array and fill it with the same value
    std::array<wchar_t, length> big_wide;
    big_wide.fill(value);
    big_wide.back() = L'\0'; // Null-terminate the string

    std::string output{};
    EXPECT_NO_THROW(WideToUtf8(big_wide.data(), output););

    EXPECT_EQ(length - 1, output.size());
  }

} // namespace oxygen::string_utils

#endif // OXYGEN_WINDOWS

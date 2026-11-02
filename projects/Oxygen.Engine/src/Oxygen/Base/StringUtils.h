//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Base/Platforms.h>

#ifdef OXYGEN_WINDOWS
#  include <windows.h> // IWYU pragma: keep

#  include <Oxygen/Base/Windows/Exceptions.h>
#else
#  include <stdexcept>
#endif

namespace oxygen::string_utils {

namespace detail {
  inline constexpr char32_t kMaxCodePoint = 0x10FFFF;

  [[nodiscard]] constexpr auto IsScalarValue(const char32_t cp) noexcept -> bool
  {
    return cp <= kMaxCodePoint && (cp < 0xD800 || cp > 0xDFFF);
  }

  //! Decodes one strict UTF-8 sequence at `pos`, which lies before the end of
  //! `in`, and advances `pos` past it. Nothing for an overlong form, a
  //! surrogate, a value above U+10FFFF or a truncated sequence.
  [[nodiscard]] constexpr auto TryDecodeUtf8(
    const std::string_view in, size_t& pos) noexcept -> std::optional<char32_t>
  {
    struct Shape {
      size_t extra;
      char32_t bits;
      char32_t min;
    };
    const auto lead = static_cast<unsigned char>(in[pos++]);
    if (lead < 0x80U) {
      return lead;
    }
    const auto shape = [lead]() -> std::optional<Shape> {
      if ((lead & 0xE0U) == 0xC0U) {
        return Shape { .extra = 1, .bits = lead & 0x1FU, .min = 0x80 };
      }
      if ((lead & 0xF0U) == 0xE0U) {
        return Shape { .extra = 2, .bits = lead & 0x0FU, .min = 0x800 };
      }
      if ((lead & 0xF8U) == 0xF0U) {
        return Shape { .extra = 3, .bits = lead & 0x07U, .min = 0x10000 };
      }
      return std::nullopt;
    }();
    if (!shape || in.size() - pos < shape->extra) {
      return std::nullopt;
    }
    auto cp = shape->bits;
    for (size_t i = 0; i < shape->extra; ++i) {
      const auto next = static_cast<unsigned char>(in[pos++]);
      if ((next & 0xC0U) != 0x80U) {
        return std::nullopt;
      }
      cp = (cp << 6U) | (next & 0x3FU);
    }
    if (cp < shape->min || !IsScalarValue(cp)) {
      return std::nullopt;
    }
    return cp;
  }
} // namespace detail

//! Whether `text` is strict UTF-8: no overlong form, surrogate, value above
//! U+10FFFF or truncated sequence.
[[nodiscard]] constexpr auto IsValidUtf8(const std::string_view text) noexcept
  -> bool
{
  size_t pos = 0;
  while (pos < text.size()) {
    if (!detail::TryDecodeUtf8(text, pos)) {
      return false;
    }
  }
  return true;
}

#ifndef OXYGEN_WINDOWS
namespace detail {
  // Locale-independent UTF-8 / UTF-16 / UTF-32 codec for platforms where
  // wchar_t holds one Unicode scalar value.
  static_assert(sizeof(wchar_t) == 4, "POSIX wide strings are UTF-32");

  //! Decode one UTF-8 sequence starting at `pos`; advances `pos`.
  [[nodiscard]] inline auto DecodeUtf8(std::string_view in, size_t& pos)
    -> char32_t
  {
    const auto cp = TryDecodeUtf8(in, pos);
    if (!cp) {
      throw std::runtime_error("Invalid UTF-8 sequence");
    }
    return *cp;
  }

  //! Append the UTF-8 encoding of a Unicode scalar value.
  template <typename OutputType>
  void AppendUtf8(OutputType& out, const char32_t cp)
  {
    using CharT = typename OutputType::value_type;
    if (!IsScalarValue(cp)) {
      throw std::runtime_error("Invalid wide character sequence");
    }
    if (cp < 0x80) {
      out.push_back(static_cast<CharT>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<CharT>(0xC0U | (cp >> 6U)));
      out.push_back(static_cast<CharT>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<CharT>(0xE0U | (cp >> 12U)));
      out.push_back(static_cast<CharT>(0x80U | ((cp >> 6U) & 0x3FU)));
      out.push_back(static_cast<CharT>(0x80U | (cp & 0x3FU)));
    } else {
      out.push_back(static_cast<CharT>(0xF0U | (cp >> 18U)));
      out.push_back(static_cast<CharT>(0x80U | ((cp >> 12U) & 0x3FU)));
      out.push_back(static_cast<CharT>(0x80U | ((cp >> 6U) & 0x3FU)));
      out.push_back(static_cast<CharT>(0x80U | (cp & 0x3FU)));
    }
  }

  //! Decode UTF-16 (with surrogate pairs) and append its UTF-8 encoding.
  /*!
   Unpaired surrogates are rejected through AppendUtf8's scalar-value check.
   */
  template <typename OutputType>
  void AppendUtf16ToUtf8(OutputType& out, const std::u16string_view in)
  {
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
      char32_t cp = in[i];
      if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < in.size()
        && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10U) + (in[++i] - 0xDC00);
      }
      AppendUtf8(out, cp);
    }
  }
} // namespace detail
#endif

namespace detail {
  //! The length of a string as the Windows conversion APIs take it.
  /*!
   They take an `int`, and read `-1` as "up to a terminator", so a length
   that does not fit is refused rather than narrowed.

   @throw std::length_error when `size` exceeds what an `int` holds.
  */
  [[nodiscard]] constexpr auto ToApiLength(const size_t size) -> int
  {
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) {
      throw std::length_error(
        "string is too long for a Windows conversion API");
    }
    return static_cast<int>(size);
  }
} // namespace detail

// Concept to ensure the output type has clear(), resize(), and data() methods
// and that T::value_type is convertible to char
template <typename T>
concept ResizableString = requires(T t) {
  { t.clear() } -> std::same_as<void>;
  { t.resize(std::declval<size_t>()) } -> std::same_as<void>;
  { t.data() } -> std::same_as<typename T::value_type*>;
  requires std::convertible_to<typename T::value_type, char>;
};

// Generic function to convert UTF-8 to wide string
template <typename InputType>
void Utf8ToWide(const InputType& in, std::wstring& out)
{
  if (in.empty()) {
    out.clear();
    return;
  }
  const std::string_view sv_in(
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<const char*>(in.data()), in.size());

#ifdef OXYGEN_WINDOWS
  // The length goes with the data, checked by ToApiLength, which the check
  // cannot see through.
  // NOLINTBEGIN(bugprone-suspicious-stringview-data-usage)
  const auto in_length = detail::ToApiLength(sv_in.size());
  const int size_needed = MultiByteToWideChar(
    CP_UTF8, MB_ERR_INVALID_CHARS, sv_in.data(), in_length, nullptr, 0);
  if (size_needed <= 0) {
    windows::WindowsException::ThrowFromLastError();
  }

  out.resize(size_needed);
  const int ret = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
    sv_in.data(), in_length, out.data(), size_needed);
  // NOLINTEND(bugprone-suspicious-stringview-data-usage)
  if (ret <= 0) {
    windows::WindowsException::ThrowFromLastError();
  }
#else
  std::wstring decoded;
  decoded.reserve(sv_in.size());
  for (size_t pos = 0; pos < sv_in.size();) {
    decoded.push_back(static_cast<wchar_t>(detail::DecodeUtf8(sv_in, pos)));
  }
  out = std::move(decoded);
#endif
}

// Overloads for different input types
inline void Utf8ToWide(const char* in, std::wstring& out)
{
  Utf8ToWide(std::string_view(in), out);
}

inline void Utf8ToWide(const char8_t* in, std::wstring& out)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  Utf8ToWide(std::string_view(reinterpret_cast<const char*>(in)), out);
}

inline void Utf8ToWide(const uint8_t* in, std::wstring& out)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  Utf8ToWide(std::string_view(reinterpret_cast<const char*>(in)), out);
}

inline void WideToUtf8(std::wstring_view in, std::wstring& out)
{
  out.assign(in.begin(), in.end());
}

// Generic function to convert wide string to UTF-8
template <ResizableString OutputType>
void WideToUtf8(const std::wstring_view& in, OutputType& out)
{
  if (in.empty()) {
    out.clear();
    return;
  }
#ifdef OXYGEN_WINDOWS
  // The length goes with the data, checked by ToApiLength, which the check
  // cannot see through.
  // NOLINTBEGIN(bugprone-suspicious-stringview-data-usage)
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  const auto in_length = detail::ToApiLength(in.size());
  const int size_needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
    in.data(), in_length, nullptr, 0, nullptr, nullptr);
  if (size_needed <= 0) {
    windows::WindowsException::ThrowFromLastError();
  }

  out.resize(size_needed);
  const int ret
    = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, in.data(), in_length,
      reinterpret_cast<char*>(out.data()), size_needed, nullptr, nullptr);
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
  // NOLINTEND(bugprone-suspicious-stringview-data-usage)
  if (ret <= 0) {
    windows::WindowsException::ThrowFromLastError();
  }
#else
  OutputType encoded;
  encoded.reserve(in.size());
  for (const wchar_t wc : in) {
    detail::AppendUtf8(encoded, static_cast<char32_t>(wc));
  }
  out = std::move(encoded);
#endif
}

// Overloads for different input types
inline void WideToUtf8(const std::wstring& in, std::string& out)
{
  WideToUtf8(std::wstring_view(in), out);
}

inline void WideToUtf8(const wchar_t* in, std::string& out)
{
  WideToUtf8(std::wstring_view(in), out);
}

inline void WideToUtf8(const char16_t* in, std::string& out)
{
#ifdef OXYGEN_WINDOWS
  // wchar_t is UTF-16 on Windows.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  WideToUtf8(std::wstring_view(reinterpret_cast<const wchar_t*>(in)), out);
#else
  // wchar_t is UTF-32 here: decode the UTF-16 text instead of reinterpreting
  // its storage as wide characters.
  std::string encoded;
  detail::AppendUtf16ToUtf8(encoded, std::u16string_view(in));
  out = std::move(encoded);
#endif
}

inline void WideToUtf8(const std::wstring& in, std::u8string& out)
{
  WideToUtf8(std::wstring_view(in), out);
}

inline void WideToUtf8(const wchar_t* in, std::u8string& out)
{
  WideToUtf8(std::wstring_view(in), out);
}

inline void WideToUtf8(const char16_t* in, std::u8string& out)
{
#ifdef OXYGEN_WINDOWS
  // wchar_t is UTF-16 on Windows.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  WideToUtf8(std::wstring_view(reinterpret_cast<const wchar_t*>(in)), out);
#else
  // wchar_t is UTF-32 here: reinterpreting UTF-16 storage as wchar_t would
  // decode every other code unit, so run the UTF-16 decoder instead.
  std::u8string encoded;
  detail::AppendUtf16ToUtf8(encoded, std::u16string_view(in));
  out = std::move(encoded);
#endif
}

inline void WideToUtf8(std::string_view in, std::u8string& out)
{
  out.assign(in.begin(), in.end());
}

inline void WideToUtf8(std::u8string_view in, std::u8string& out)
{
  out.assign(in.begin(), in.end());
}

inline void WideToUtf8(std::string_view in, std::string& out)
{
  out.assign(in.begin(), in.end());
}

inline void WideToUtf8(std::u8string_view in, std::string& out)
{
  out.assign(in.begin(), in.end());
}

} // namespace oxygen::string_utils

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Base/Platforms.h>

#if defined(OXYGEN_WINDOWS)
#  include <windows.h> // IWYU pragma: keep

#  include <Oxygen/Base/Windows/Exceptions.h>
#else
#  include <stdexcept>
#endif

namespace oxygen::string_utils {

#if !defined(OXYGEN_WINDOWS)
namespace detail {
  // Strict, locale-independent UTF-8 / UTF-16 / UTF-32 codec for platforms
  // where wchar_t holds one Unicode scalar value. Rejects overlong forms,
  // surrogate code points, values above U+10FFFF and truncated sequences.
  static_assert(sizeof(wchar_t) == 4, "POSIX wide strings are UTF-32");

  inline constexpr char32_t kMaxCodePoint = 0x10FFFF;

  [[nodiscard]] constexpr auto IsScalarValue(const char32_t cp) noexcept
  {
    return cp <= kMaxCodePoint && (cp < 0xD800 || cp > 0xDFFF);
  }

  //! Decode one UTF-8 sequence starting at `pos`; advances `pos`.
  [[nodiscard]] inline auto DecodeUtf8(std::string_view in, size_t& pos)
    -> char32_t
  {
    const auto fail
      = [] { throw std::runtime_error("Invalid UTF-8 sequence"); };
    const auto lead = static_cast<unsigned char>(in[pos++]);
    if (lead < 0x80) {
      return lead;
    }
    size_t extra = 0;
    char32_t cp = 0;
    char32_t min = 0;
    if ((lead & 0xE0U) == 0xC0U) {
      extra = 1;
      cp = lead & 0x1FU;
      min = 0x80;
    } else if ((lead & 0xF0U) == 0xE0U) {
      extra = 2;
      cp = lead & 0x0FU;
      min = 0x800;
    } else if ((lead & 0xF8U) == 0xF0U) {
      extra = 3;
      cp = lead & 0x07U;
      min = 0x10000;
    } else {
      fail();
    }
    if (in.size() - pos < extra) {
      fail();
    }
    for (size_t i = 0; i < extra; ++i) {
      const auto next = static_cast<unsigned char>(in[pos++]);
      if ((next & 0xC0U) != 0x80U) {
        fail();
      }
      cp = (cp << 6U) | (next & 0x3FU);
    }
    if (cp < min || !IsScalarValue(cp)) {
      fail();
    }
    return cp;
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
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view sv_in(
    reinterpret_cast<const char*>(in.data()), in.size());

#if defined(OXYGEN_WINDOWS)
  const int size_needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
    sv_in.data(), static_cast<int>(sv_in.size()), nullptr, 0);
  if (size_needed <= 0) {
    windows::WindowsException::ThrowFromLastError();
  }

  out.resize(size_needed);
  const int ret = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
    sv_in.data(), static_cast<int>(sv_in.size()), out.data(), size_needed);
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
#if defined(OXYGEN_WINDOWS)
  int size_needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
    in.data(), static_cast<int>(in.size()), nullptr, 0, nullptr, nullptr);
  if (size_needed <= 0) {
    windows::WindowsException::ThrowFromLastError();
  }

  out.resize(size_needed);
  const int ret = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, in.data(),
    static_cast<int>(in.size()),
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<char*>(out.data()), size_needed, nullptr, nullptr);
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
#if defined(OXYGEN_WINDOWS)
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
#if defined(OXYGEN_WINDOWS)
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

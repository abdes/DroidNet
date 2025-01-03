//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Platforms.h"
#if defined(OXYGEN_WINDOWS)

#include <windows.h>

#include "Oxygen/Base/Windows/Exceptions.h"

namespace oxygen::string_utils {

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
  void Utf8ToWide(const InputType& in, std::wstring& out) {
    const std::string_view sv_in(reinterpret_cast<const char*>(in.data()), in.size());
    if (sv_in.empty()) {
      out.clear();
      return;
    }

    const int size_needed = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      sv_in.data(),
      static_cast<int>(sv_in.size()),
      nullptr,
      0);
    if (size_needed <= 0) {
      windows::WindowsException::ThrowFromLastError();
    }

    out.resize(size_needed);
    const int ret = MultiByteToWideChar(
      CP_UTF8,
      MB_ERR_INVALID_CHARS,
      sv_in.data(),
      static_cast<int>(sv_in.size()),
      out.data(),
      size_needed);
    if (ret <= 0) {
      windows::WindowsException::ThrowFromLastError();
    }
  }

  // Overloads for different input types
  inline void Utf8ToWide(const char* in, std::wstring& out) {
    return Utf8ToWide(std::string_view(in), out);
  }

  inline void Utf8ToWide(const char8_t* in, std::wstring& out) {
    return Utf8ToWide(std::string_view(reinterpret_cast<const char*>(in)), out);
  }

  inline void Utf8ToWide(const uint8_t* in, std::wstring& out) {
    return Utf8ToWide(std::string_view(reinterpret_cast<const char*>(in)), out);
  }

  inline void WideToUtf8(std::wstring_view in, std::wstring& out) {
    out.assign(in.begin(), in.end());
  }

  // Generic function to convert wide string to UTF-8
  template <ResizableString OutputType>
  void WideToUtf8(const std::wstring_view& in, OutputType& out) {
    if (in.empty()) {
      out.clear();
      return;
    }

    int size_needed = WideCharToMultiByte(
      CP_UTF8,
      WC_ERR_INVALID_CHARS,
      in.data(),
      static_cast<int>(in.size()),
      nullptr,
      0,
      nullptr,
      nullptr);
    if (size_needed <= 0) {
      windows::WindowsException::ThrowFromLastError();
    }

    out.resize(size_needed);
    int ret = WideCharToMultiByte(
      CP_UTF8,
      WC_ERR_INVALID_CHARS,
      in.data(),
      static_cast<int>(in.size()),
      reinterpret_cast<char*>(out.data()),
      size_needed,
      nullptr,
      nullptr);
    if (ret <= 0) {
      windows::WindowsException::ThrowFromLastError();
    }
  }

  // Overloads for different input types
  inline void WideToUtf8(const std::wstring& in, std::string& out) {
    return WideToUtf8(std::wstring_view(in), out);
  }

  inline void WideToUtf8(const wchar_t* in, std::string& out) {
    return WideToUtf8(std::wstring_view(in), out);
  }

  inline void WideToUtf8(const char16_t* in, std::string& out) {
    return WideToUtf8(std::wstring_view(reinterpret_cast<const wchar_t*>(in)), out);
  }

  inline void WideToUtf8(const std::wstring& in, std::u8string& out) {
    return WideToUtf8(std::wstring_view(in), out);
  }

  inline void WideToUtf8(const wchar_t* in, std::u8string& out) {
    return WideToUtf8(std::wstring_view(in), out);
  }

  inline void WideToUtf8(const char16_t* in, std::u8string& out) {
    return WideToUtf8(std::wstring_view(reinterpret_cast<const wchar_t*>(in)), out);
  }

  inline void WideToUtf8(std::string_view in, std::u8string& out) {
    out.assign(in.begin(), in.end());
  }

  inline void WideToUtf8(std::u8string_view in, std::u8string& out) {
    out.assign(in.begin(), in.end());
  }

  inline void WideToUtf8(std::string_view in, std::string& out) {
    out.assign(in.begin(), in.end());
  }

  inline void WideToUtf8(std::u8string_view in, std::string& out) {
    out.assign(in.begin(), in.end());
  }

}  // namespace oxygen::string_utils

#endif // OXYGEN_WINDOWS

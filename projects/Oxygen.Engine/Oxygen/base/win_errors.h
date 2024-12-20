//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <comdef.h>
#include <memory>
#include <stdexcept>
#include <system_error>

#include "oxygen/api_export.h"


namespace oxygen {
  enum class ComErrorEnum;
}

// Make com_error_enum known to std library
template <>
struct std::is_error_code_enum<oxygen::ComErrorEnum> : std::true_type {};

namespace oxygen {
  // Enum for classifying HRESULT error codes
  enum class ComErrorEnum {};

  // Access the COM error category
  OXYGEN_API auto ComCategory() noexcept -> const std::error_category&;

  // Function to create std::error_code from com_error_enum
  inline auto make_error_code(ComErrorEnum e) noexcept -> std::error_code
  {
    return { static_cast<int>(e), ComCategory() };
  }

  // Custom error category for COM errors
  class ComErrorCategory final : public std::error_category
  {
  public:
    [[nodiscard]] auto name() const noexcept -> const char* override;
    [[nodiscard]] auto message(int hr) const->std::string override;
    [[nodiscard]] auto default_error_condition(int hr) const noexcept -> std::error_condition override;
  };

  // Factory functions to create std::system_error from HRESULT
  auto ComToSystemError(HRESULT hr, const std::string& msg) -> std::system_error;
  auto ComToSystemError(HRESULT hr, const char* msg) -> std::system_error;
  auto ComToSystemError(HRESULT hr, const wchar_t* msg) -> std::system_error;
  auto ComToSystemError(HRESULT hr, const std::wstring& msg) -> std::system_error;
  auto ComToSystemError(const _com_error& e) -> std::system_error;
  OXYGEN_API auto ComToSystemError(HRESULT hr, IErrorInfo* help) -> std::system_error;

  // Function to throw std::system_error based on COM error
  [[noreturn]] void __stdcall ThrowTranslatedComError(HRESULT hr, IErrorInfo* help = nullptr);

  // Helper functions for wide-to-ANSI string conversion
  namespace detail {
    auto ToNarrow(BSTR msg) -> std::unique_ptr<char[]>;
    std::unique_ptr<char[]> ToNarrow(const wchar_t* msg);
  }

  // Inline function to call a function and check the HRESULT
  inline void CheckResult(const HRESULT hr) {
    if (FAILED(hr)) {
      ThrowTranslatedComError(hr);
    }
  }

  inline auto ComErrorCategory::name() const noexcept -> const char*
  {
    return "com";
  }

  inline auto ComErrorCategory::message(int hr) const -> std::string
  {
#ifdef _UNICODE
    const auto narrow = detail::ToNarrow(_com_error{ hr }.ErrorMessage());
    return narrow.get();
#else
    return _com_error{ hr }.ErrorMessage();
#endif
  }

  inline auto ComErrorCategory::default_error_condition(int hr) const noexcept -> std::error_condition
  {
    if (HRESULT_CODE(hr) || hr == 0)
      return std::system_category().default_error_condition(HRESULT_CODE(hr));
    else
      return { hr, ComCategory() };
  }

  inline auto ComToSystemError(const HRESULT hr, const std::string& msg) -> std::system_error
  {
    return ComToSystemError(hr, msg.c_str());
  }

  inline auto ComToSystemError(HRESULT hr, const char* msg) -> std::system_error
  {
    return { {static_cast<ComErrorEnum>(hr)}, msg };
  }

  inline auto ComToSystemError(const HRESULT hr, const wchar_t* msg) -> std::system_error
  {
    return ComToSystemError(hr, *msg ? detail::ToNarrow(msg).get() : "");
  }

  inline auto ComToSystemError(const HRESULT hr, const std::wstring& msg) -> std::system_error
  {
    return ComToSystemError(hr, msg.c_str());
  }

  inline auto ComToSystemError(const _com_error& e) -> std::system_error
  {
    return ComToSystemError(e.Error(), IErrorInfoPtr{ e.ErrorInfo(), false });
  }

  [[noreturn]] inline void __stdcall ThrowTranslatedComError(const HRESULT hr, IErrorInfo* help) {
    throw ComToSystemError(hr, help);
  }

  namespace detail {

    inline auto ToNarrow(const BSTR msg) -> std::unique_ptr<char[]>
    {
      return std::unique_ptr<char[]>{_com_util::ConvertBSTRToString(msg)};
    }

    inline auto ToNarrow(const wchar_t* msg) -> std::unique_ptr<char[]>
    {
      static_assert(std::is_same_v<wchar_t*, BSTR>);
      return ToNarrow(const_cast<wchar_t*>(msg));
    }

  } // namespace detail

} // namespace oxygen::renderer::d3d12::detail

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/base/platform.h"
#if defined(OXYGEN_WINDOWS)

#include <comdef.h>
#include <memory>
#include <stdexcept>
#include <system_error>

// Enum for classifying HRESULT error codes
enum class ComErrorEnum {};

// Make com_error_enum known to std library
template <>
struct std::is_error_code_enum<ComErrorEnum> : std::true_type {};

namespace oxygen {

  // Function to create std::error_code from com_error_enum
  auto make_error_code(ComErrorEnum e) noexcept -> std::error_code;

  // Access the COM error category
  const std::error_category& com_category() noexcept;

  // Custom error category for COM errors
  class com_error_category : public std::error_category
  {
  public:
    [[nodiscard]] const char* name() const noexcept override;
    [[nodiscard]] auto message(int hr) const->std::string override;
    [[nodiscard]] auto default_error_condition(int hr) const noexcept -> std::error_condition override;
  };

  // Factory functions to create std::system_error from HRESULT
  auto com_to_system_error(HRESULT hr, const std::string& msg) -> std::system_error;
  auto com_to_system_error(HRESULT hr, const char* msg) -> std::system_error;
  auto com_to_system_error(HRESULT hr, const wchar_t* msg) -> std::system_error;
  auto com_to_system_error(HRESULT hr, const std::wstring& msg) -> std::system_error;
  auto com_to_system_error(const _com_error& e) -> std::system_error;
  auto com_to_system_error(HRESULT hr, IErrorInfo* help) -> std::system_error;

  // Function to throw std::system_error based on COM error
  [[noreturn]] void __stdcall throw_translated_com_error(HRESULT hr, IErrorInfo* help = nullptr);

  // Helper functions for wide-to-ANSI string conversion
  namespace detail {
    auto to_narrow(BSTR msg) -> std::unique_ptr<char[]>;
    auto to_narrow(const wchar_t* msg) -> std::unique_ptr<char[]>;
  }

  inline auto make_error_code(ComErrorEnum e) noexcept -> std::error_code
  {
    return { static_cast<int>(e), com_category() };
  }

  inline auto com_category() noexcept -> const std::error_category&
  {
    static com_error_category ecat;
    return ecat;
  }

  inline auto com_error_category::name() const noexcept -> const char*
  {
    return "com";
  }

  inline auto com_error_category::message(int hr) const -> std::string
  {
#ifdef _UNICODE
    const auto narrow = detail::to_narrow(_com_error{ hr }.ErrorMessage());
    return narrow.get();
#else
    return _com_error{ hr }.ErrorMessage();
#endif
  }

  inline auto com_error_category::default_error_condition(int hr) const noexcept -> std::error_condition
  {
    if (HRESULT_CODE(hr) || hr == 0)
      return std::system_category().default_error_condition(HRESULT_CODE(hr));
    else
      return { hr, com_category() };
  }

  inline auto com_to_system_error(const HRESULT hr, const std::string& msg) -> std::system_error
  {
    return com_to_system_error(hr, msg.c_str());
  }

  inline auto com_to_system_error(HRESULT hr, const char* msg) -> std::system_error
  {
    return { {static_cast<ComErrorEnum>(hr)}, msg };
  }

  inline auto com_to_system_error(const HRESULT hr, const wchar_t* msg) -> std::system_error
  {
    return com_to_system_error(hr, *msg ? detail::to_narrow(msg).get() : "");
  }

  inline auto com_to_system_error(const HRESULT hr, const std::wstring& msg) -> std::system_error
  {
    return com_to_system_error(hr, msg.c_str());
  }

  inline auto com_to_system_error(const _com_error& e) -> std::system_error
  {
    return com_to_system_error(e.Error(), IErrorInfoPtr{ e.ErrorInfo(), false });
  }

  inline auto com_to_system_error(HRESULT hr, IErrorInfo* help) -> std::system_error
  {
    using ComCstr = std::unique_ptr<OLECHAR[], decltype(SysFreeString)*>;
    auto get_description = [](IErrorInfo* info) -> ComCstr {
      BSTR description = nullptr;
      if (info) auto _ = info->GetDescription(&description);
      return { description, &SysFreeString };
      };

    ComCstr&& description = get_description(help);
    if (const unsigned int length = description ? SysStringLen(description.get()) : 0) {
      const unsigned int n = length;
      const OLECHAR ch0 = std::exchange(description[0], L'\0');
      for (;;) {
        if (description[n - 1] == L'\r' || description[n - 1] == L'\n' || description[n - 1] == L'.') {
          continue;
        }
        break;
      }
      if (n < length && n) description[n] = L'\0';
      if (n) description[0] = ch0;

      return com_to_system_error(hr, description.get());
    }

    return com_to_system_error(hr);
  }

  [[noreturn]] inline void __stdcall throw_translated_com_error(const HRESULT hr, IErrorInfo* help) {
    throw com_to_system_error(hr, help);
  }

  namespace detail {

    inline std::unique_ptr<char[]> to_narrow(const BSTR msg) {
      return std::unique_ptr<char[]>{_com_util::ConvertBSTRToString(msg)};
    }

    inline std::unique_ptr<char[]> to_narrow(const wchar_t* msg) {
      static_assert(std::is_same_v<wchar_t*, BSTR>);
      return to_narrow(const_cast<wchar_t*>(msg));
    }

  } // namespace detail

} // namespace oxygen

#endif // OXYGEN_WINDOWS

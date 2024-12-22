//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Platform.h"
#include "StringUtils.h"
#if defined(OXYGEN_WINDOWS)

#include <optional>
#include <system_error>
#include <string>
#include <string_view>

#include <comdef.h>

#include "Oxygen/api_export.h"

namespace oxygen {
  enum class ComErrorEnum;
}

// Make com_error_enum known to std library
template <>
struct std::is_error_code_enum<oxygen::ComErrorEnum> : std::true_type {};

namespace oxygen::windows {

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
    [[nodiscard]] auto name() const noexcept -> const char* override { return "com"; }
    [[nodiscard]] auto default_error_condition(const int hr) const noexcept -> std::error_condition override
    {
      return (HRESULT_CODE(hr) || hr == 0)
        ? std::system_category().default_error_condition(HRESULT_CODE(hr))
        : std::error_condition{ hr, ComCategory() };
    }
    OXYGEN_API [[nodiscard]] auto message(int hr) const->std::string override;
  };

  template <typename T>
  concept StringType = requires(T t) {
    { std::string_view(t) } -> std::convertible_to<std::string_view>;
    { std::u8string_view(t) } -> std::convertible_to<std::u8string_view>;
    { std::wstring_view(t) } -> std::convertible_to<std::wstring_view>;
  } || std::is_same_v<T, const char8_t*> || std::is_same_v<T, const char*> || std::is_same_v<T, const wchar_t*>;

  // ComError class derived from std::system_error
  class ComError final : public std::system_error
  {
  public:
    explicit ComError(const ComErrorEnum error_code)
      : std::system_error(static_cast<int>(error_code), ComCategory())
    {
    }

    ComError(ComErrorEnum error_code, const char* msg)
      : std::system_error(static_cast<int>(error_code), ComCategory(), msg)
    {
    }

    ComError(ComErrorEnum error_code, const std::string& msg)
      : std::system_error(static_cast<int>(error_code), ComCategory(), msg)
    {
    }

    static void __declspec(noreturn) Throw(const ComErrorEnum error_code, const std::string& utf8_message)
    {
      throw ComError(error_code, utf8_message);
    }

    HRESULT GetHR() const noexcept { return code().value(); }

    OXYGEN_API [[nodiscard]] auto what() const noexcept -> const char* override;

  private:
    mutable std::optional<std::string> message_;
  };


  namespace detail {

    // Non-templated function to handle COM errors with a UTF-8 message
    OXYGEN_API void HandleComErrorImpl(HRESULT hr, const std::string& utf8_message);

    // Function to handle COM errors
    template <StringType T>
    void HandleComError(const HRESULT hr, T message = nullptr) {
      std::string utf8_message{};
      try {
        string_utils::WideToUtf8(message, utf8_message);
      }
      catch (const std::exception& e) {
        utf8_message.append("__not_available__ (").append(e.what()).append(")");
      }
      HandleComErrorImpl(hr, utf8_message);
    }

  }  // namespace detail

  // Inline function to call a function and check the HRESULT
  template <StringType T>
  void ThrowOnFailed(const HRESULT hr, T message) {
    if (FAILED(hr)) {
      detail::HandleComError(hr, message);
    }
  }

  inline void ThrowOnFailed(const HRESULT hr) {
    if (FAILED(hr)) {
      detail::HandleComError<const char*>(hr);
    }
  }

}  // namespace oxygen

#endif // OXYGEN_WINDOWS

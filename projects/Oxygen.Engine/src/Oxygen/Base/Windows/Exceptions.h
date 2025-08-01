//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Platforms.h>
#if defined(OXYGEN_WINDOWS)

#  include <exception>
#  include <optional>
#  include <string>
#  include <system_error>

#  include <windows.h>

#  include <Oxygen/Base/api_export.h>

namespace oxygen::windows {

class WindowsException : public std::system_error {
public:
  static auto FromLastError() noexcept -> std::exception_ptr
  {
    return FromErrorCode(GetLastError());
  }
  OXYGEN_BASE_API static auto FromErrorCode(DWORD error_code) noexcept
    -> std::exception_ptr;

  static void __declspec(noreturn) ThrowFromErrorCode(const DWORD error_code)
  {
    std::rethrow_exception(FromErrorCode(error_code));
  }
  static void __declspec(noreturn) ThrowFromLastError()
  {
    ThrowFromErrorCode(GetLastError());
  }

  auto GetErrorCode() const noexcept -> DWORD { return code().value(); }

  [[nodiscard]] OXYGEN_BASE_API auto what() const noexcept -> const
    char* override;

protected:
  explicit WindowsException(const DWORD error_code)
    : std::system_error(static_cast<int>(error_code), std::system_category())
  {
  }

private:
  mutable std::optional<std::string> message_;
};

} // namespace oxygen::windows

#endif // OXYGEN_WINDOWS

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <system_error>

#include <Oxygen/Serio/Detail/SystemErrors.h>
#include <Oxygen/Serio/Error.h>

namespace oxygen::serio {
namespace {

  //! Whether a generic (`std::errc`) code means a Serio condition.
  auto IsGenericCondition(const int value, const Error condition) noexcept
    -> bool
  {
    switch (static_cast<std::errc>(value)) {
    case std::errc::no_such_file_or_directory:
      return condition == Error::kNotFound;
    case std::errc::permission_denied:
    case std::errc::operation_not_permitted:
      return condition == Error::kAccessDenied;
    case std::errc::no_space_on_device:
      return condition == Error::kNoSpace;
    case std::errc::io_error:
      return condition == Error::kIo;
    case std::errc::is_a_directory:
      return condition == Error::kIsDirectory;
    case std::errc::filename_too_long:
      return condition == Error::kPathTooLong;
    default:
      return false;
    }
  }

  class SerioErrorCategory final : public std::error_category {
  public:
    [[nodiscard]] auto name() const noexcept -> const char* override
    {
      return "oxygen::serio";
    }

    [[nodiscard]] auto message(const int value) const -> std::string override
    {
      switch (static_cast<Error>(value)) {
      case Error::kEndOfData:
        return "end of data";
      case Error::kOutOfBounds:
        return "out of bounds";
      case Error::kTooLarge:
        return "too large";
      case Error::kDigestMismatch:
        return "digest mismatch";
      case Error::kNotFound:
        return "not found";
      case Error::kAccessDenied:
        return "access denied";
      case Error::kSharingViolation:
        return "sharing violation";
      case Error::kNoSpace:
        return "no space";
      case Error::kIo:
        return "device error";
      case Error::kIsDirectory:
        return "is a directory";
      case Error::kPathTooLong:
        return "path too long";
      }
      return "unknown serio error";
    }

    [[nodiscard]] auto equivalent(const std::error_code& code,
      const int condition) const noexcept -> bool override
    {
      const auto serio_condition = static_cast<Error>(condition);
      if (code.category() == *this) {
        return code.value() == condition;
      }
      if (code.category() == std::system_category()) {
        return detail::IsSystemErrorCondition(code.value(), serio_condition);
      }
      if (code.category() == std::generic_category()) {
        return IsGenericCondition(code.value(), serio_condition);
      }
      return false;
    }
  };

} // namespace

auto ErrorCategory() noexcept -> const std::error_category&
{
  static const SerioErrorCategory category;
  return category;
}

} // namespace oxygen::serio

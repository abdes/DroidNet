//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <winerror.h>

#include <Oxygen/Serio/Detail/SystemErrors.h>
#include <Oxygen/Serio/Error.h>

namespace oxygen::serio::detail {

auto IsSystemErrorCondition(const int value, const Error condition) noexcept
  -> bool
{
  switch (static_cast<DWORD>(value)) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
  case ERROR_BAD_NETPATH:
  case ERROR_BAD_NET_NAME:
    return condition == Error::kNotFound;
  case ERROR_ACCESS_DENIED:
  case ERROR_WRITE_PROTECT:
    return condition == Error::kAccessDenied;
  case ERROR_SHARING_VIOLATION:
  case ERROR_LOCK_VIOLATION:
    return condition == Error::kSharingViolation;
  case ERROR_FILENAME_EXCED_RANGE:
    return condition == Error::kPathTooLong;
  case ERROR_DISK_FULL:
  case ERROR_HANDLE_DISK_FULL:
  case ERROR_DISK_QUOTA_EXCEEDED:
    return condition == Error::kNoSpace;
  case ERROR_IO_DEVICE:
  case ERROR_CRC:
  case ERROR_READ_FAULT:
  case ERROR_WRITE_FAULT:
  case ERROR_GEN_FAILURE:
  case ERROR_SECTOR_NOT_FOUND:
  case ERROR_DEVICE_NOT_CONNECTED:
  case ERROR_NOT_READY:
    return condition == Error::kIo;
  default:
    return false;
  }
}

} // namespace oxygen::serio::detail

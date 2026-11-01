//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cerrno>

#include <Oxygen/Serio/Detail/SystemErrors.h>

namespace oxygen::serio::detail {

auto IsSystemErrorCondition(const int value, const Error condition) noexcept
  -> bool
{
  switch (value) {
  case ENOENT:
  case ENOTDIR:
    return condition == Error::kNotFound;
  case EACCES:
  case EPERM:
  case EROFS:
  // A write through a descriptor opened read-only: Windows reports the same
  // case as ERROR_ACCESS_DENIED.
  case EBADF:
    return condition == Error::kAccessDenied;
  case ETXTBSY:
    return condition == Error::kSharingViolation;
  case ENOSPC:
  case EDQUOT:
    return condition == Error::kNoSpace;
  case EIO:
  case ENXIO:
    return condition == Error::kIo;
  case EISDIR:
    return condition == Error::kIsDirectory;
  default:
    return false;
  }
}

} // namespace oxygen::serio::detail

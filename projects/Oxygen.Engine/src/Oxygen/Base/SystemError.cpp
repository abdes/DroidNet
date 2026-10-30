//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <system_error>

#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Base/SystemError.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <errhandlingapi.h>
#else
#  include <cerrno>
#endif

namespace oxygen::base {

auto LastSystemError() noexcept -> std::error_code
{
#ifdef OXYGEN_WINDOWS
  return { static_cast<int>(GetLastError()), std::system_category() };
#else
  return { errno, std::system_category() };
#endif
}

} // namespace oxygen::base

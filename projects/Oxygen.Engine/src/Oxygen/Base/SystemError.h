//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <system_error>

#include <Oxygen/Base/api_export.h>

namespace oxygen::base {

//! The calling thread's last OS error: `GetLastError()` on Windows, `errno`
//! elsewhere, always in `std::system_category()`.
/*!
 Call it right after the failed OS call, before anything that may change the
 last error. Compare the result portably through `std::errc` conditions.
*/
OXGN_BASE_NDAPI auto LastSystemError() noexcept -> std::error_code;

} // namespace oxygen::base

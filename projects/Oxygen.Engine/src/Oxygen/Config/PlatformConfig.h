//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen {

struct PlatformConfig {
  bool headless { false }; //!< Run without any windows.
  uint32_t thread_pool_size { 0 }; //!< 0 = no thread pool.
};

} // namespace platform

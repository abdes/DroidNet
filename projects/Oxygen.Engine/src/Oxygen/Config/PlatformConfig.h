//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen {

struct PlatformConfig {

  //! When `true`, run the engine without any windows.
  bool headless { false };

  //! Number of threads to use for the thread pool. `0` means no thread pool.
  uint32_t thread_pool_size { kDefaultThreadPoolSize };

private:
  static constexpr uint32_t kDefaultThreadPoolSize = 4;
};

} // namespace oxygen

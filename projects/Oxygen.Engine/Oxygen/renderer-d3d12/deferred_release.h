//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>

#include "oxygen/base/macros.h"

namespace oxygen::renderer::direct3d12 {

  class IDeferredReleaseCoordinator
  {
  public:
    using DeferredReleaseHandler = std::function<void(size_t)>;

    IDeferredReleaseCoordinator() = default;
    virtual ~IDeferredReleaseCoordinator() = default;

    OXYGEN_MAKE_NON_COPYABLE(IDeferredReleaseCoordinator);
    OXYGEN_MAKE_NON_MOVEABLE(IDeferredReleaseCoordinator);

    virtual void RegisterDeferredReleases(DeferredReleaseHandler handler) const = 0;
  };

}  // namespace oxygen::renderer::direct3d12

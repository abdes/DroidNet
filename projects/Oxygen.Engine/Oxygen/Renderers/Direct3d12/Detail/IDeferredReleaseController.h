//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>

#include "oxygen/base/Macros.h"

namespace oxygen::renderer::d3d12::detail {

  class IDeferredReleaseController
  {
  public:
    using DeferredReleaseHandler = std::function<void(size_t)>;

    IDeferredReleaseController() = default;
    virtual ~IDeferredReleaseController() = default;

    OXYGEN_MAKE_NON_COPYABLE(IDeferredReleaseController);
    OXYGEN_MAKE_NON_MOVEABLE(IDeferredReleaseController);

    virtual void RegisterDeferredReleases(DeferredReleaseHandler handler) const = 0;
  };

}  // namespace oxygen::renderer::d3d12

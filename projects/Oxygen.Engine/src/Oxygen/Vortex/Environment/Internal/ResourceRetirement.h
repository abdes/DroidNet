//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <utility>

#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

namespace oxygen::vortex::environment::internal {

//! Retire a resource and its shader-visible descriptors at the same GPU fence.
template <typename Resource>
auto RetireEnvironmentResource(
  Graphics& graphics, std::shared_ptr<Resource>& resource) -> void
{
  if (!resource)
    return;
  auto* registry = &graphics.GetResourceRegistry();
  graphics.GetDeferredReclaimer().RegisterDeferredAction(
    [registry, resource = std::move(resource)]() mutable {
      if (registry->Contains(*resource))
        registry->UnRegisterResource(*resource);
      resource.reset();
    });
}

} // namespace oxygen::vortex::environment::internal

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Graphics/Common/MixinDeferredRelease.h"

namespace oxygen::renderer {

[[nodiscard]] PerFrameResourceManager& GetPerFrameResourceManager();

template <typename T>
void DeferredObjectRelease(T*& resource,
  PerFrameResourceManager& resource_manager) noexcept
{
  if (resource) {
    resource_manager.RegisterDeferredRelease(resource);
  }
}

//! Immediately releases a resource that has a `Release()` method, and
//! sets the provided pointer to `nullptr`.
template <typename T>
void DeferredObjectRelease(std::shared_ptr<T>& resource,
  PerFrameResourceManager& resource_manager) noexcept
  requires HasReleaseMethod<T>
{
  if (resource) {
    resource_manager.RegisterDeferredRelease(resource);
  }
}

} // namespace oxygen::renderer::d3d12::detail

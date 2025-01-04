//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Graphics/Common/DeferredObjectRelease.h"
#include "Oxygen/Graphics/Direct3D12/Graphics.h"

namespace oxygen::graphics::d3d12::detail {

template <typename T>
void DeferredObjectRelease(T*& resource) noexcept
  requires HasReleaseMethod<T>
{
  if (resource) {
    graphics::d3d12::detail::GetPerFrameResourceManager().RegisterDeferredRelease(resource);
  }
}

//! Immediately releases a resource that has a `Release()` method, and
//! sets the provided pointer to `nullptr`.
template <typename T>
void DeferredObjectRelease(std::shared_ptr<T>& resource) noexcept
  requires HasReleaseMethod<T>
{
  if (resource) {
    graphics::d3d12::detail::GetPerFrameResourceManager().RegisterDeferredRelease(resource);
  }
}

} // namespace oxygen::graphics::d3d12::detail

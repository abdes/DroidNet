//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>

namespace oxygen::graphics {

//! Registers a resource with a `Release()` method for deferred release, and
//! resets the variable holding it to null. The resource's reference count is
//! __NOT__ decremented until the release actually happends.
template <HasReleaseMethod T>
void DeferredObjectRelease(T*& resource,
    detail::PerFrameResourceManager& resource_manager) noexcept
{
    if (resource) {
        resource_manager.RegisterDeferredRelease(resource);
        resource = nullptr;
    }
}

//! Registers a resource held by a `shared_ptr` for deferred release, and resets
//! the pointer. The resource's reference count will stay > 0 for as long as it
//! is waiting to be released.
template <HasReleaseMethod T>
void DeferredObjectRelease(std::shared_ptr<T>& resource,
    detail::PerFrameResourceManager& resource_manager) noexcept
    requires HasReleaseMethod<T>
{
    if (resource) {
        resource_manager.RegisterDeferredRelease(resource);
        resource.reset();
    }
}

//! Immediately releases a resource held by a `unique_ptr`, and
//! sets the provided pointer to `nullptr`.
template <HasReleaseMethod T>
void DeferredObjectRelease(std::unique_ptr<T>& resource,
    detail::PerFrameResourceManager& resource_manager) noexcept
    requires HasReleaseMethod<T>
{
    if (resource) {
        resource_manager.RegisterDeferredRelease(resource.release());
    }
}

} // namespace oxygen::graphics

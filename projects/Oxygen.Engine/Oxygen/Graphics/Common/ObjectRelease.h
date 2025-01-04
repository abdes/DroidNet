//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

namespace oxygen::graphics {

//! Requires a type `T` to have `Release()` method. Typically the case for
//! `Direct3D` or similar objects.
template <typename T>
concept HasReleaseMethod = requires(T t) {
  { t.Release() };
};

//! Immediately releases a resource that has a `Release()` method, and
//! sets the provided pointer to `nullptr`.
template <typename T>
void ObjectRelease(T*& resource) noexcept
  requires HasReleaseMethod<T>
{
  if (resource) {
    resource->Release();
    resource = nullptr;
  }
}

//! Immediately releases a resource that has a `Release()` method, resets the
//! reference to it, and sets the provided pointer to `nullptr`.
template <typename T>
void ObjectRelease(std::shared_ptr<T>& resource) noexcept
  requires HasReleaseMethod<T>
{
  if (resource) {
    resource->Release();
    resource = nullptr;
  }
}

//! Immediately releases a resource with no `Release()` method, resets the
//! reference to it and sets the provided pointer to `nullptr`.
template <typename T>
void ObjectRelease(std::shared_ptr<T>& resource) noexcept
{
  if (resource) {
    resource = nullptr;
  }
}

} // namespace oxygen::graphics::d3d12::detail

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

//! Immediately releases a resource  held by a `shared_ptr`, resulting in its
//! reference count being decremented, and sets the provided pointer to null.
template <typename T> void ObjectRelease(std::shared_ptr<T>& resource) noexcept
{
  // This will reset the shared_ptr, call the object destructor and nullify
  // the referenced variable/member. Note that the actual release of the
  // graphics resource must be done in the destructor of the object.
  resource = {};
}

//! Immediately releases a resource held by a `unique_ptr` (which may or may not
//! have a custom deleter), and sets the provided pointer to null.
template <typename T> void ObjectRelease(std::unique_ptr<T>& resource) noexcept
{
  // This will reset the shared_ptr, call the object destructor and nullify
  // the referenced variable/member. Note that the actual release of the
  // graphics resource must be done in the destructor of the object.
  resource = {};
}

} // namespace oxygen::graphics

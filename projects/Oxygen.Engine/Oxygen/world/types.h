//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/base/resource_handle.h"

#define DECLARE_RESOURCE(scope, Name)                                          \
  using Name##Id = ResourceHandle;                                             \
  namespace scope {                                                            \
  struct Descriptor;                                                           \
  }                                                                            \
  using Name##Descriptor = scope::Descriptor;                                  \
  class Name##;

namespace oxygen::world {

  // ---------------------------------------------------------------------------
  // Resources:
  // ---------------------------------------------------------------------------
  // For each resource type, declare a ResourceTypeT constant and a resource,
  // using the `DECLARE_RESOURCE` macro. This will declare a specific name for
  // resource handle and add forward type decclarations for the resource and its
  // descriptor.
  // ---------------------------------------------------------------------------

  namespace resources {
    constexpr ResourceHandle::ResourceTypeT kGameEntity = 1;
    constexpr ResourceHandle::ResourceTypeT kTransform = 2;
  }  // namespace resources

  DECLARE_RESOURCE(entity, GameEntity)
  DECLARE_RESOURCE(transform, Transform)

}  // namespace oxygen::world

#undef DECLARE_RESOURCE

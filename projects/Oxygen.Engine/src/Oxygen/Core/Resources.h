//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ResourceHandle.h>

#define DECLARE_RESOURCE(scope, Name)           \
    using Name##Id = ResourceHandle;            \
    namespace scope {                           \
    struct Descriptor;                          \
    }                                           \
    using Name##Descriptor = scope::Descriptor; \
    class Name;

namespace oxygen::world {

// ---------------------------------------------------------------------------
// Resources:
// ---------------------------------------------------------------------------
// For each resource type, declare a ResourceTypeT constant and a resource,
// using the `DECLARE_RESOURCE` macro. This will declare a specific name for
// resource handle and add forward type declarations for the resource and its
// descriptor.
// ---------------------------------------------------------------------------

namespace resources {
    // Reserved resource types for engine internal objects with IDs
    constexpr ResourceHandle::ResourceTypeT kReserved1 = 1;
    constexpr ResourceHandle::ResourceTypeT kReserved2 = 2;
    constexpr ResourceHandle::ResourceTypeT kReserved3 = 3;
    constexpr ResourceHandle::ResourceTypeT kReserved4 = 4;
    constexpr ResourceHandle::ResourceTypeT kReserved5 = 5;
    constexpr ResourceHandle::ResourceTypeT kReserved6 = 6;
    constexpr ResourceHandle::ResourceTypeT kReserved7 = 7;
    constexpr ResourceHandle::ResourceTypeT kReserved8 = 8;
    constexpr ResourceHandle::ResourceTypeT kReserved9 = 9;
    constexpr ResourceHandle::ResourceTypeT kReserved10 = 10;

    // World resources
    constexpr ResourceHandle::ResourceTypeT kSceneNode = 13;
} // namespace resources

DECLARE_RESOURCE(scene, SceneNode)

} // namespace oxygen::world

#undef DECLARE_RESOURCE

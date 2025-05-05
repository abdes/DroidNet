//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::graphics {

//! Represents the usage state of a resource in a 3D rendering engine.
//! Supports Direct3D 12 and Vulkan interchangeably.
enum class ResourceState : uint32_t {
    //! The resource state is unknown to the engine and is managed by the application (None).
    kUnknown = 0,

    //! The resource state is defined but uninitialized (Graphics, Compute).
    kUndefined = 1 << 0,

    //! The resource is accessed as a vertex buffer (Graphics).
    kVertexBuffer = 1 << 1,

    //! The resource is accessed as a constant (uniform) buffer (Graphics, Compute).
    kConstantBuffer = 1 << 2,

    //! The resource is accessed as an index buffer (Graphics).
    kIndexBuffer = 1 << 3,

    //! The resource is accessed as a render target (Graphics).
    kRenderTarget = 1 << 4,

    //! The resource is used for unordered access (UAV) (Graphics, Compute).
    kUnorderedAccess = 1 << 5,

    //! The resource is used for writable depth-stencil operations (Graphics).
    kDepthWrite = 1 << 6,

    //! The resource is used for read-only depth-stencil operations (Graphics).
    kDepthRead = 1 << 7,

    //! The resource is accessed as a shader resource (Graphics, Compute).
    kShaderResource = 1 << 8,

    //! The resource is used as the destination for stream output (Graphics).
    kStreamOut = 1 << 9,

    //! The resource is used as an indirect draw/dispatch argument buffer (Graphics, Compute).
    kIndirectArgument = 1 << 10,

    //! The resource is used as the destination in a copy operation (Graphics, Compute, Transfer).
    kCopyDest = 1 << 11,

    //! The resource is used as the source in a copy operation (Graphics, Compute, Transfer).
    kCopySource = 1 << 12,

    //! The resource is used as the destination in a resolve operation (Graphics).
    kResolveDest = 1 << 13,

    //! The resource is used as the source in a resolve operation (Graphics).
    kResolveSource = 1 << 14,

    //! The resource is used as an input attachment in a render pass (Graphics).
    kInputAttachment = 1 << 15,

    //! The resource is used for swapchain presentation (Graphics).
    kPresent = 1 << 16,

    //! The resource is used as vertex/index/instance data in AS builds or as source in AS copy operations (Graphics, Compute).
    kBuildAccelStructureRead = 1 << 17,

    //! The resource is used as the target for AS building or AS copy operations (Graphics, Compute).
    kBuildAccelStructureWrite = 1 << 18,

    //! The resource is used as an acceleration structure shader resource in a ray tracing operation (Graphics, Compute).
    kRayTracing = 1 << 19,

    //! The resource is readable, but transitioning to this state may cause a pipeline stall or cache flush (Graphics, Compute, Transfer).
    kCommon = 1 << 20,

    //! The resource is used as a shading rate image (Graphics).
    kShadingRate = 1 << 21,

    //! A generic read state for multiple resource usages combined (Graphics, Compute).
    //! Avoid using this state unless necessary, as it is not optimal.
    kGenericRead = kVertexBuffer | kConstantBuffer | kIndexBuffer | kShaderResource | kIndirectArgument | kCopySource
};

//! String representation of enum values in `QueueFamilyType`.
OXYGEN_GFX_API auto to_string(ResourceState value) -> const char*;

} // namespace oxygen::graphics

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits> // Required for std::underlying_type_t used by the macro

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Represents the usage state of a resource in a 3D rendering engine.
//! Supports Direct3D 12 and Vulkan interchangeably.
enum class ResourceStates : uint32_t {
    //! The resource state is unknown to the engine and is managed by the application (None).
    kUnknown = 0,

    //! The resource state is defined but uninitialized (Graphics, Compute).
    kUndefined = OXYGEN_FLAG(0),

    //! The resource is accessed as a vertex buffer (Graphics).
    kVertexBuffer = OXYGEN_FLAG(1),

    //! The resource is accessed as a constant (uniform) buffer (Graphics, Compute).
    kConstantBuffer = OXYGEN_FLAG(2),

    //! The resource is accessed as an index buffer (Graphics).
    kIndexBuffer = OXYGEN_FLAG(3),

    //! The resource is accessed as a render target (Graphics).
    kRenderTarget = OXYGEN_FLAG(4),

    //! The resource is used for unordered access (UAV) (Graphics, Compute).
    kUnorderedAccess = OXYGEN_FLAG(5),

    //! The resource is used for writable depth-stencil operations (Graphics).
    kDepthWrite = OXYGEN_FLAG(6),

    //! The resource is used for read-only depth-stencil operations (Graphics).
    kDepthRead = OXYGEN_FLAG(7),

    //! The resource is accessed as a shader resource (Graphics, Compute).
    kShaderResource = OXYGEN_FLAG(8),

    //! The resource is used as the destination for stream output (Graphics).
    kStreamOut = OXYGEN_FLAG(9),

    //! The resource is used as an indirect draw/dispatch argument buffer (Graphics, Compute).
    kIndirectArgument = OXYGEN_FLAG(10),

    //! The resource is used as the destination in a copy operation (Graphics, Compute, Transfer).
    kCopyDest = OXYGEN_FLAG(11),

    //! The resource is used as the source in a copy operation (Graphics, Compute, Transfer).
    kCopySource = OXYGEN_FLAG(12),

    //! The resource is used as the destination in a resolve operation (Graphics).
    kResolveDest = OXYGEN_FLAG(13),

    //! The resource is used as the source in a resolve operation (Graphics).
    kResolveSource = OXYGEN_FLAG(14),

    //! The resource is used as an input attachment in a render pass (Graphics).
    kInputAttachment = OXYGEN_FLAG(15),

    //! The resource is used for swapchain presentation (Graphics).
    kPresent = OXYGEN_FLAG(16),

    //! The resource is used as vertex/index/instance data in AS builds or as source in AS copy operations (Graphics, Compute).
    kBuildAccelStructureRead = OXYGEN_FLAG(17),

    //! The resource is used as the target for AS building or AS copy operations (Graphics, Compute).
    kBuildAccelStructureWrite = OXYGEN_FLAG(18),

    //! The resource is used as an acceleration structure shader resource in a ray tracing operation (Graphics, Compute).
    kRayTracing = OXYGEN_FLAG(19),

    //! The resource is readable, but transitioning to this state may cause a pipeline stall or cache flush (Graphics, Compute, Transfer).
    kCommon = OXYGEN_FLAG(20),

    //! The resource is used as a shading rate image (Graphics).
    kShadingRate = OXYGEN_FLAG(21),

    //! A generic read state for multiple resource usages combined (Graphics, Compute).
    //! Avoid using this state unless necessary, as it is not optimal.
    kGenericRead = kVertexBuffer | kConstantBuffer | kIndexBuffer | kShaderResource | kIndirectArgument | kCopySource
};

// Enable bitwise operations for ResourceStates
OXYGEN_DEFINE_FLAGS_OPERATORS(ResourceStates)

//! String representation of enum values in `QueueFamilyType`.
OXYGEN_GFX_API auto to_string(ResourceStates value) -> const char*;

//! Specifies the tracking mode for resource state transitions managed by the
//! `CommandList`.
enum class ResourceStateTrackingMode : uint8_t {
    //! Default tracking mode. The application will manually update the resource
    //! state using `UpdateResourceState` method in `CommandList`. The command
    //! list will insert necessary barriers, avoiding redundant transitions.
    kDefault,

    //! Similar to `kDefault`, but the command list will always ensure that
    //! resource is in the initial state, provided when `TrackResourceState` was
    //! called, when it leaves the command list.
    kKeepInitialState,

    //! This is useful for static resources like material textures and vertex
    //! buffers: after initialization, their contents never change, and they can
    //! be kept in the same state without ever being transitioned. Permanent
    //! resources cannot be transitioned using `UpdateResourceState`, and the
    //! command list will discard such requests and log them as errors in
    //! development builds.
    kPermanentState
};

//! String representation of enum values in `QueueFamilyType`.
OXYGEN_GFX_API auto to_string(ResourceStateTrackingMode value) -> const char*;

} // namespace oxygen::graphics

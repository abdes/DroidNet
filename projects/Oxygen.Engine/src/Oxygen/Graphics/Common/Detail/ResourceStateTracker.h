//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/TrackableResource.h>

namespace oxygen::graphics::detail {

// Barrier description structures - these contain all information needed to create barriers
// but don't represent the actual API-specific barriers

// Memory barrier description
struct MemoryBarrierDesc {
    NativeObject resource;

    bool operator==(const MemoryBarrierDesc& other) const
    {
        return resource == other.resource;
    }
};

// Buffer barrier description
struct BufferBarrierDesc {
    NativeObject resource;
    ResourceStates before;
    ResourceStates after;

    bool operator==(const BufferBarrierDesc& other) const
    {
        return resource == other.resource && before == other.before
            && after == other.after;
    }
};

// Texture barrier description
struct TextureBarrierDesc {
    NativeObject resource;
    ResourceStates before;
    ResourceStates after;
    // Could add additional texture-specific fields like mip levels, array slices, etc.

    bool operator==(const TextureBarrierDesc& other) const
    {
        return resource == other.resource && before == other.before
            && after == other.after;
    }
};

// The barrier descriptor - a variant that can hold any type of barrier description
using BarrierDesc = std::variant<BufferBarrierDesc, TextureBarrierDesc, MemoryBarrierDesc>;

// Abstract Barrier base class - now just a handle to a barrier description
class Barrier {
public:
    // Constructor with descriptor only (type deduced from descriptor)
    explicit Barrier(BarrierDesc desc)
        : descriptor_(std::move(desc))
    {
    }

    virtual ~Barrier() = default;

    // Get the barrier descriptor
    const BarrierDesc& GetDescriptor() const { return descriptor_; }

    // Get the resource for this barrier
    NativeObject GetResource() const
    {
        return std::visit([](auto&& desc) -> NativeObject { return desc.resource; },
            descriptor_);
    }

private:
    BarrierDesc descriptor_;
};

OXYGEN_GFX_API auto to_string(const Barrier& barrier) -> std::string;

//! Resource state tracker and barrier management for command lists.
/*!
 Oxygen `CommandList` provides assistance in managing resource states and the
 barriers needed for their transitions. Since command lists may be recorded in
 parallel, and then executed out of order, there is no reliable way of fully and
 automatically managing resource state transitions without the help of the
 application. The command list must know in which state a resource is when it
 enters the command list, and what state it should be in when it leaves the
 command list. This is done through a collaboration between the resource state
 tracker and the application:

 Use `TrackResourceState` to enable state tracking for the graphics resource,
 and provide the initial state information to the command list. This is usually
 done right after the resource is created in its initial state, and will not
 produce a state transition. The method also accepts a `TrackingMode` parameter,
 which determines how the command list will track the resource state.

   - `kDefault`: The application will manually update the resource state using
     `UpdateResourceState`. The command list will insert necessary barriers,
     avoiding redundant transitions.

   - `kKeepInitialState`: This is similar to `kDefault`, but the command list
     will always ensure that resource is in the initial state when it leaves the
     command list.

   - `kPermanentState`: This is useful for static resources like material
     textures and vertex buffers: after initialization, their contents never
     change, and they can be kept in the same state without ever being
     transitioned. Permanent resources cannot be transitioned using
     `UpdateResourceState`, and the command list will discard such requests and
     log them as errors in development builds.

 \note All barriers created for state transitions are only placed into an
 internal accumulator, and must be explicitly pushed into the Graphics backend
 by calling the `FlushBarriers` method of the command list.
*/
class ResourceStateTracker {
public:
    ResourceStateTracker() = default;
    ~ResourceStateTracker() = default;

    OXYGEN_MAKE_NON_COPYABLE(ResourceStateTracker)
    OXYGEN_MAKE_NON_MOVABLE(ResourceStateTracker)

    template <HoldsNativeResource T>
    void BeginTrackingResourceState(const T& resource, ResourceStates initial_state, bool keep_initial_state = false)
    {
    }

    template <HoldsNativeResource T>
    void EnableAutoMemoryBarriers(const T& resource)
    {
    }

    template <HoldsNativeResource T>
    void DisableAutoMemoryBarriers(const T& resource)
    {
    }

    template <HoldsNativeResource T>
    void RequireResourceState(const T& resource, ResourceStates state, bool is_permanent = false)
    {
    }

    void OnCommandListClosed()
    {
    }

    void OnCommandListSubmitted()
    {
    }

private:
    struct BasicTrackingInfo {
        ResourceStates current_state;
        bool is_permanent = false;
        bool is_auto_memory_barriers_enabled = false;
    };

    struct BufferTrackingInfo : public BasicTrackingInfo {
    };

    struct TextureTrackingInfo : public BasicTrackingInfo {
        // TODO(abdes): add texture-specific fields like mip levels, array slices, etc.
    };

    // The barrier descriptor - a variant that can hold any type of barrier description
    using TrackingInfo = std::variant<BufferTrackingInfo, TextureTrackingInfo>;

    std::unordered_map<NativeObject, TrackingInfo> tracking_;
};

} // namespace oxygen::graphics::detail

// TODO(abdes): feature, similar to NVRHI, that may be implemented later
// As part of state tracking, NVRHI will place UAV barriers between successive
// uses of the same resource in UnorderedAccess state. That might not always be
// desired: for example, some rendering methods address the same texture as a
// UAV from the pixel shader, and do not care about ordering of accesses for
// different meshes. For such use cases, the command list provides the
// setEnableUavBarriersForTexture/Buffer(bool enable) methods that can be used
// to temporarily remove such UAV barriers. On DX11, these methods map to
// NVAPI_D3D11_Begin/EndUAVOverlap calls. Conversely, it is sometimes necessary
// to place UAV barriers more often than NVRHI would do it, which is at every
// setGraphicsState or similar call. For example, there may be a sequence of
// compute passes operating on a buffer that use the same shader but different
// constants. As updating constants does not require a call to one of those
// state setting functions, an automatic barrier will not be placed. To place a
// UAV barrier manually, use the nvrhi::utils::texture/bufferUavBarrier
// functions.

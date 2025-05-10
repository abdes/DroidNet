//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/TrackableResource.h>
#include <Oxygen/Graphics/Common/api_export.h>

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

    ResourceStates GetStateBefore()
    {
        return std::visit(
            [](auto&& desc) -> ResourceStates {
                using T = std::decay_t<decltype(desc)>;
                if constexpr (std::is_same_v<T, BufferBarrierDesc>) {
                    return desc.before;
                } else if constexpr (std::is_same_v<T, TextureBarrierDesc>) {
                    return desc.before;
                } else {
                    return ResourceStates::kUnknown;
                }
            },
            descriptor_);
    }

    ResourceStates GetStateAfter()
    {
        return std::visit(
            [](auto&& desc) -> ResourceStates {
                using T = std::decay_t<decltype(desc)>;
                if constexpr (std::is_same_v<T, BufferBarrierDesc>) {
                    return desc.after;
                } else if constexpr (std::is_same_v<T, TextureBarrierDesc>) {
                    return desc.after;
                } else {
                    return ResourceStates::kUnknown;
                }
            },
            descriptor_);
    }

    void AppendState(ResourceStates state)
    {
        std::visit(
            [state](auto&& desc) {
                using T = std::decay_t<decltype(desc)>;

                if constexpr (std::is_same_v<T, BufferBarrierDesc>) {
                    desc.after |= state;
                } else if constexpr (std::is_same_v<T, TextureBarrierDesc>) {
                    desc.after |= state;
                }
            },
            descriptor_);
    }

    // Check if the barrier is a memory barrier
    bool IsMemoryBarrier() const
    {
        return std::holds_alternative<MemoryBarrierDesc>(descriptor_);
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

    template <Trackable T>
    void BeginTrackingResourceState(
        const T& resource,
        const ResourceStates initial_state,
        const bool keep_initial_state = false)
    {
        // Calling BeginTrackingResourceState on a resource that is already being tracked
        // will throw an exception.
        NativeObject native_object = resource.GetNativeResource();
        auto it = tracking_.find(native_object);
        if (it != tracking_.end()) {
            throw std::runtime_error("Resource is already being tracked");
        }

        if constexpr (IsBuffer<T>) {
            tracking_.emplace(native_object, BufferTrackingInfo(initial_state, keep_initial_state));
        } else if constexpr (IsTexture<T>) {
            tracking_.emplace(native_object, TextureTrackingInfo(initial_state, keep_initial_state));
        } else {
            throw std::runtime_error("Unsupported resource type");
        }
    }

    template <Trackable T>
    void EnableAutoMemoryBarriers(const T& resource)
    {
        auto& ti = GetTrackingInfo(resource.GetNativeResource());
        std::visit(
            [&](auto& info) {
                info.enable_auto_memory_barriers = true;
            },
            ti);
    }

    template <Trackable T>
    void DisableAutoMemoryBarriers(const T& resource)
    {
        auto& ti = GetTrackingInfo(resource.GetNativeResource());
        std::visit(
            [&](auto& info) {
                info.enable_auto_memory_barriers = false;
            },
            ti);
    }

    // Require a resource to be in a specific state (non-permanent)
    template <Trackable T>
    void RequireResourceState(
        const T& resource,
        ResourceStates required_state)
    {
        if constexpr (IsBuffer<T>) {
            RequireBufferState(resource, required_state, false);
        } else if constexpr (IsTexture<T>) {
            RequireTextureState(resource, required_state, false);
        } else {
            throw std::runtime_error("Unsupported resource type");
        }
    }

    // Require a resource to be in a specific state permanently (no further changes allowed)
    template <Trackable T>
    void RequireResourceStateFinal(
        const T& resource,
        ResourceStates required_state)
    {
        if constexpr (IsBuffer<T>) {
            RequireBufferState(resource, required_state, true);
        } else if constexpr (IsTexture<T>) {
            RequireTextureState(resource, required_state, true);
        } else {
            throw std::runtime_error("Unsupported resource type");
        }
    }

    // Get all pending barriers
    [[nodiscard]] auto GetPendingBarriers() const -> const std::vector<Barrier>&
    {
        return pending_barriers_;
    }

    // Clear all tracking data
    OXYGEN_GFX_API void Clear();

    OXYGEN_GFX_API void OnCommandListClosed();

    OXYGEN_GFX_API void OnCommandListSubmitted();

private:
    struct BasicTrackingInfo {
        ResourceStates initial_state;
        ResourceStates current_state;

        bool enable_auto_memory_barriers { true };

        bool is_permanent { false };
        bool keep_initial_state { false };

        bool first_memory_barrier_inserted { false };
    };

    struct BufferTrackingInfo : public BasicTrackingInfo {
        BufferTrackingInfo(ResourceStates initial_state, bool keep_initial_state)
        {
            this->initial_state = initial_state;
            this->current_state = initial_state;
            this->keep_initial_state = keep_initial_state;
            // All other members use BasicTrackingInfo's defaults
        }
    };

    struct TextureTrackingInfo : public BasicTrackingInfo {
        TextureTrackingInfo(ResourceStates initial_state, bool keep_initial_state)
        {
            this->initial_state = initial_state;
            this->current_state = initial_state;
            this->keep_initial_state = keep_initial_state;
            // All other members use BasicTrackingInfo's defaults
        }
    };

    // The barrier descriptor - a variant that can hold any type of barrier description
    using TrackingInfo = std::variant<BufferTrackingInfo, TextureTrackingInfo>;

    OXYGEN_GFX_API auto GetTrackingInfo(const NativeObject& resource) -> TrackingInfo&;

    OXYGEN_GFX_API void RequireBufferState(const Buffer& buffer, ResourceStates required_state, bool is_permanent);
    OXYGEN_GFX_API void RequireTextureState(const Texture& texture, ResourceStates required_state, bool is_permanent);

    // Create a barrier descriptor for buffer resources
    auto CreateBufferBarrierDesc(
        const NativeObject& native_object,
        ResourceStates before,
        ResourceStates after) -> BufferBarrierDesc
    {
        return BufferBarrierDesc { .resource = native_object, .before = before, .after = after };
        // TODO: Could add buffer-specific fields here  or keep a reference to
        // the buffer object itself
    }

    // Create a barrier descriptor for texture resources
    auto CreateTextureBarrierDesc(
        const NativeObject& native_object,
        ResourceStates before,
        ResourceStates after) -> TextureBarrierDesc
    {
        return TextureBarrierDesc { .resource = native_object, .before = before, .after = after };
        // TODO: Could add texture-specific fields here (mip levels, array
        // slices, etc.) or keep a reference to the texture object itself
    }

    std::unordered_map<NativeObject, TrackingInfo> tracking_;
    std::vector<Barrier> pending_barriers_;
};

} // namespace oxygen::graphics::detail

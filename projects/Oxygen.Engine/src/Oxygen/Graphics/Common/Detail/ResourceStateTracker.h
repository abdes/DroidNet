//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/TrackableResource.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics::detail {

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
     `UpdateResourceState`. The command list will insert the necessary barriers,
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

    [[nodiscard]] auto GetPendingBarriers() const -> std::span<const Barrier>
    {
        return pending_barriers_;
    }

    [[nodiscard]] auto HasPendingBarriers() const
    {
        return !pending_barriers_.empty();
    }

    // Clear all tracking data
    OXYGEN_GFX_API void Clear();

    // Clear pending barriers
    OXYGEN_GFX_API void ClearPendingBarriers();

    OXYGEN_GFX_API void OnCommandListClosed();

    OXYGEN_GFX_API void OnCommandListSubmitted();

private:
    struct BasicTrackingInfo {
        ResourceStates initial_state { ResourceStates::kUnknown };
        ResourceStates current_state { ResourceStates::kUnknown };

        bool enable_auto_memory_barriers { true };

        bool is_permanent { false };
        bool keep_initial_state { false };

        bool first_memory_barrier_inserted { false };

        [[nodiscard]] auto NeedsTransition(const ResourceStates required_state) const
        {
            return current_state != required_state;
        }

        [[nodiscard]] auto NeedsMemoryBarrier(const ResourceStates required_state) const
        {
            // Requested state includes UnorderedAccess, AND
            return ((required_state & ResourceStates::kUnorderedAccess) == ResourceStates::kUnorderedAccess) && (
                       // We are auto inserting memory barriers, OR
                       enable_auto_memory_barriers ||
                       // memory barriers are manually managed, and this is the first time
                       // a transition for UnorderedAccess is requested
                       !first_memory_barrier_inserted);
        }
    };

    struct BufferTrackingInfo : public BasicTrackingInfo {
        BufferTrackingInfo(const ResourceStates initial_state, const bool keep_initial_state)
        {
            this->initial_state = initial_state;
            this->current_state = initial_state;
            this->keep_initial_state = keep_initial_state;
            // All other members use BasicTrackingInfo's defaults
        }
    };

    struct TextureTrackingInfo : public BasicTrackingInfo {
        TextureTrackingInfo(const ResourceStates initial_state, const bool keep_initial_state)
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

    //! Validates state transition requests for resources that may have previously transitioned to a permanent state.
    /*!
     For resources which state has been marked as permanent, this method enforces that their state cannot be changed. If
     a state transition is attempted for a permanent resource to a state different from its current state, an error is
     logged and an exception is thrown.

     \return `true` if the resource state is permanent and the requested state is not different (indicating that no
             further processing is needed), `false` if the resource state is not permanent (allowing state transition)
     \throws std::runtime_error If attempting to change a permanent resource's state.
    */
    static auto HandlePermanentState(const BasicTrackingInfo& tracking,
        ResourceStates required_state,
        const char* resource_type_name) -> bool;

    // Attempts to merge a new state transition with an existing pending barrier
    // Returns true if successfully merged, false if a new barrier is needed
    template <typename BarrierDescType>
    auto TryMergeWithExistingTransition(
        const NativeObject& native_object,
        ResourceStates& current_state,
        ResourceStates required_state) -> bool;

    std::unordered_map<NativeObject, TrackingInfo> tracking_;
    std::vector<Barrier> pending_barriers_;
};

} // namespace oxygen::graphics::detail

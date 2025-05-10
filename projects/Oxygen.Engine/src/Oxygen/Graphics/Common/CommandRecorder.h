//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/VariantHelpers.h> // For always_false_v
#include <memory>
#include <span>

#include <glm/vec4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/TrackableResource.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

namespace detail {
    class ResourceStateTracker;
    class Barrier;
} // namespace detail

class CommandList;
class CommandQueue;
class IShaderByteCode;
class Buffer;
class Texture;
class RenderTarget;
class Renderer;

enum ClearFlags : uint8_t {
    kClearFlagsColor = (1 << 0),
    kClearFlagsDepth = (1 << 1),
    kClearFlagsStencil = (1 << 2),
};

class CommandRecorder {
public:
    OXYGEN_GFX_API CommandRecorder(CommandList* command_list, CommandQueue* target_queue);

    OXYGEN_GFX_API virtual ~CommandRecorder(); // Definition moved to .cpp

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder)
    OXYGEN_MAKE_NON_MOVABLE(CommandRecorder)

    [[nodiscard]] auto GetTargetQueue() const { return target_queue_; }

    OXYGEN_GFX_API virtual void Begin();
    OXYGEN_GFX_API virtual auto End() -> CommandList*;

    // Graphics commands
    virtual void Clear(uint32_t flags, uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors, float depth_value, uint8_t stencil_value) = 0;
    virtual void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void SetVertexBuffers(uint32_t num, const std::shared_ptr<Buffer>* vertex_buffers, const uint32_t* strides, const uint32_t* offsets) = 0;

    virtual void SetViewport(float left, float width, float top, float height, float min_depth, float max_depth) = 0;
    virtual void SetScissors(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0;
    virtual void SetRenderTarget(std::unique_ptr<RenderTarget> render_target) = 0;
    virtual void SetPipelineState(const std::shared_ptr<IShaderByteCode>& vertex_shader, const std::shared_ptr<IShaderByteCode>& pixel_shader) = 0;

    //! @{
    //! Resource state management and barriers.
    /*!
     These template methods provide a generic and convenient API for managing
     resource states and barriers for any resource type. If the resource type is
     not supported, a compile-time error will be generated.
    */

    template <Trackable T>
    void BeginTrackingResourceState(
        const T& resource,
        const ResourceStates initial_state,
        const bool keep_initial_state = false)
    {
        if constexpr (IsBuffer<T>) {
            DoBeginTrackingResourceState(static_cast<const Buffer&>(resource), initial_state, keep_initial_state);
        } else if constexpr (IsTexture<T>) {
            DoBeginTrackingResourceState(static_cast<const Texture&>(resource), initial_state, keep_initial_state);
        } else {
            static_assert(always_false_v<T>, "Unsupported Trackable resource type for BeginTrackingResourceState");
        }
    }

    template <Trackable T>
    void EnableAutoMemoryBarriers(const T& resource)
    {
        if constexpr (IsBuffer<T>) {
            DoEnableAutoMemoryBarriers(static_cast<const Buffer&>(resource));
        } else if constexpr (IsTexture<T>) {
            DoEnableAutoMemoryBarriers(static_cast<const Texture&>(resource));
        } else {
            static_assert(always_false_v<T>, "Unsupported Trackable resource type for EnableAutoMemoryBarriers");
        }
    }

    template <Trackable T>
    void DisableAutoMemoryBarriers(const T& resource)
    {
        if constexpr (IsBuffer<T>) {
            DoDisableAutoMemoryBarriers(static_cast<const Buffer&>(resource));
        } else if constexpr (IsTexture<T>) {
            DoDisableAutoMemoryBarriers(static_cast<const Texture&>(resource));
        } else {
            static_assert(always_false_v<T>, "Unsupported Trackable resource type for DisableAutoMemoryBarriers");
        }
    }

    template <Trackable T>
    void RequireResourceState(
        const T& resource,
        const ResourceStates state)
    {
        if constexpr (IsBuffer<T>) {
            DoRequireResourceState(static_cast<const Buffer&>(resource), state);
        } else if constexpr (IsTexture<T>) {
            DoRequireResourceState(static_cast<const Texture&>(resource), state);
        } else {
            static_assert(always_false_v<T>, "Unsupported Trackable resource type for RequireResourceState");
        }
    }

    template <Trackable T>
    void RequireResourceStateFinal(
        const T& resource,
        const ResourceStates state)
    {
        if constexpr (IsBuffer<T>) {
            DoRequireResourceStateFinal(static_cast<const Buffer&>(resource), state);
        } else if constexpr (IsTexture<T>) {
            DoRequireResourceStateFinal(static_cast<const Texture&>(resource), state);
        } else {
            static_assert(always_false_v<T>, "Unsupported Trackable resource type for RequireResourceStateFinal");
        }
    }

    // Process all pending barriers and execute them
    void FlushBarriers();

    //! @}

protected:
    [[nodiscard]] auto GetCommandList() const -> CommandList* { return command_list_; }

    //! Executes the given collection of barriers.
    /*!
     This is a backend-specific implementation that takes a collection of
     barriers and issues the appropriate commands to the GPU to execute them.
    */
    virtual void ExecuteBarriers(std::span<const detail::Barrier> barriers) = 0;

private:
    friend class Renderer;
    OXYGEN_GFX_API virtual void OnSubmitted();

    //! @{
    //! Private non-template dispatch methods for resource state tracking and
    //! barrier management.

    void DoBeginTrackingResourceState(const Buffer& resource, ResourceStates initial_state, bool keep_initial_state);
    void DoBeginTrackingResourceState(const Texture& resource, ResourceStates initial_state, bool keep_initial_state);

    void DoEnableAutoMemoryBarriers(const Buffer& resource);
    void DoEnableAutoMemoryBarriers(const Texture& resource);

    void DoDisableAutoMemoryBarriers(const Buffer& resource);
    void DoDisableAutoMemoryBarriers(const Texture& resource);

    void DoRequireResourceState(const Buffer& resource, ResourceStates state);
    void DoRequireResourceState(const Texture& resource, ResourceStates state);

    void DoRequireResourceStateFinal(const Buffer& resource, ResourceStates state);
    void DoRequireResourceStateFinal(const Texture& resource, ResourceStates state);

    //! @}

    CommandList* command_list_;
    CommandQueue* target_queue_;

    std::unique_ptr<detail::ResourceStateTracker> resource_state_tracker_;
};

} // namespace oxygen::graphics

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
#include <Oxygen/Graphics/Common/Types/Scissors.h>
#include <Oxygen/Graphics/Common/Types/TrackableResource.h>
#include <Oxygen/Graphics/Common/Types/ViewPort.h>
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
class Framebuffer;

enum ClearFlags : uint8_t {
    kClearFlagsColor = (1 << 0),
    kClearFlagsDepth = (1 << 1),
    kClearFlagsStencil = (1 << 2),
};

class CommandRecorder {
public:
    enum class SubmissionMode {
        Immediate,
        Deferred
    };

    OXYGEN_GFX_API CommandRecorder(CommandList* command_list, CommandQueue* target_queue, SubmissionMode mode = SubmissionMode::Immediate);

    OXYGEN_GFX_API virtual ~CommandRecorder(); // Definition moved to .cpp

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder)
    OXYGEN_MAKE_NON_MOVABLE(CommandRecorder)

    [[nodiscard]] auto GetTargetQueue() const { return target_queue_; }

    OXYGEN_GFX_API virtual void Begin();
    OXYGEN_GFX_API virtual auto End() -> CommandList*;

    // Graphics commands
    virtual void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void SetVertexBuffers(uint32_t num, const std::shared_ptr<Buffer>* vertex_buffers, const uint32_t* strides, const uint32_t* offsets) = 0;

    virtual void SetViewport(const ViewPort& viewport) = 0;
    virtual void SetScissors(const Scissors& scissors) = 0;
    virtual void SetPipelineState(const std::shared_ptr<IShaderByteCode>& vertex_shader, const std::shared_ptr<IShaderByteCode>& pixel_shader) = 0;

    virtual void InitResourceStatesFromFramebuffer(const Framebuffer& framebuffer) = 0;
    virtual void BindFrameBuffer(const Framebuffer& framebuffer) = 0;

    virtual void ClearTextureFloat(Texture* _t, TextureSubResourceSet sub_resources, const Color& clearColor) = 0;

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
    OXYGEN_GFX_API void FlushBarriers();

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
    SubmissionMode GetSubmissionMode() const { return submission_mode_; }
    SubmissionMode submission_mode_ { SubmissionMode::Immediate };

    //! @{
    //! Private non-template dispatch methods for resource state tracking and
    //! barrier management.

    OXYGEN_GFX_API void DoBeginTrackingResourceState(const Buffer& resource, ResourceStates initial_state, bool keep_initial_state);
    OXYGEN_GFX_API void DoBeginTrackingResourceState(const Texture& resource, ResourceStates initial_state, bool keep_initial_state);

    OXYGEN_GFX_API void DoEnableAutoMemoryBarriers(const Buffer& resource);
    OXYGEN_GFX_API void DoEnableAutoMemoryBarriers(const Texture& resource);

    OXYGEN_GFX_API void DoDisableAutoMemoryBarriers(const Buffer& resource);
    OXYGEN_GFX_API void DoDisableAutoMemoryBarriers(const Texture& resource);

    OXYGEN_GFX_API void DoRequireResourceState(const Buffer& resource, ResourceStates state);
    OXYGEN_GFX_API void DoRequireResourceState(const Texture& resource, ResourceStates state);

    OXYGEN_GFX_API void DoRequireResourceStateFinal(const Buffer& resource, ResourceStates state);
    OXYGEN_GFX_API void DoRequireResourceStateFinal(const Texture& resource, ResourceStates state);

    //! @}

    CommandList* command_list_;
    CommandQueue* target_queue_;

    std::unique_ptr<detail::ResourceStateTracker> resource_state_tracker_;
};

} // namespace oxygen::graphics

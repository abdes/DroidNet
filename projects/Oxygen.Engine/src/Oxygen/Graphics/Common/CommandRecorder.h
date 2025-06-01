//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Base/AlwaysFalse.h>
#include <Oxygen/Base/Macros.h>
// ReSharper disable once CppUnusedIncludeDirective - For always_false_v
#include <Oxygen/Base/VariantHelpers.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
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
class Renderer;
class Framebuffer;
class NativeObject;

class CommandRecorder {
public:
    enum class SubmissionMode : uint8_t {
        kImmediate,
        kDeferred
    };

    //=== Lifecycle ===-------------------------------------------------------//

    OXYGEN_GFX_API CommandRecorder(CommandList* command_list, CommandQueue* target_queue, SubmissionMode mode = SubmissionMode::kImmediate);
    OXYGEN_GFX_API virtual ~CommandRecorder(); // Definition moved to .cpp

    OXYGEN_MAKE_NON_COPYABLE(CommandRecorder)
    OXYGEN_MAKE_NON_MOVABLE(CommandRecorder)

    [[nodiscard]] auto GetTargetQueue() const { return target_queue_; }

    //=== Command List Control ===--------------------------------------------//

    OXYGEN_GFX_API virtual void Begin();
    OXYGEN_GFX_API virtual auto End() -> CommandList*;

    //=== Pipeline State and Bindless Setup ===-------------------------------//

    //! Sets the graphics pipeline state for subsequent draw calls.
    /*!
     Call this before issuing any draw commands to ensure the correct shaders,
     input layout, and fixed-function state are bound. The provided pipeline
     description must match the resources and framebuffer formats in use.

     Will create and set a root signature for bindless rendering, set the
     shader visible descriptor heaps accordingly, get a cached pipeline state or
     create a new one and set it.

     Best Practices:
      - Always set the pipeline state after binding the framebuffer and before
        drawing.
      - Ensure the descriptor layout and shader expectations match the pipeline
        description.
      - Avoid redundant state changes for performance.

     \param desc The graphics pipeline state description to bind.
    */
    virtual void SetPipelineState(GraphicsPipelineDesc desc) = 0;

    //! Sets the compute pipeline state for subsequent dispatch calls.
    /*!
     Call this before issuing any compute dispatch commands to ensure the
     correct compute shader and state are bound. The pipeline description must
     match the resources expected by the compute shader.

     \param desc The compute pipeline state description to bind.
    */
    virtual void SetPipelineState(ComputePipelineDesc desc) = 0;

    //=== Direct Binding ===--------------------------------------------------//

    virtual void SetGraphicsRootConstantBufferView(
        uint32_t root_parameter_index,
        uint64_t buffer_gpu_address)
        = 0;

    virtual void SetComputeRootConstantBufferView(
        uint32_t root_parameter_index,
        uint64_t buffer_gpu_address)
        = 0;

    //=== Render State ===----------------------------------------------------//

    virtual void SetRenderTargets(
        std::span<NativeObject> rtvs,
        std::optional<NativeObject> dsv)
        = 0;

    virtual void SetViewport(const ViewPort& viewport) = 0;
    virtual void SetScissors(const Scissors& scissors) = 0;

    //=== Draw and Resource Binding Commands ===------------------------------//

    virtual void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void DrawIndexed(uint32_t index_num, uint32_t instances_num, uint32_t index_offset, int32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void Dispatch(uint32_t thread_group_count_x, uint32_t thread_group_count_y, uint32_t thread_group_count_z) = 0;
    virtual void SetVertexBuffers(uint32_t num, const std::shared_ptr<Buffer>* vertex_buffers, const uint32_t* strides, const uint32_t* offsets) = 0;

    //=== Framebuffer and Resource Operations ===-----------------------------//

    // TODO: Legacy API to be removed when render passes are implemented.
    virtual void BindFrameBuffer(const Framebuffer& framebuffer) = 0;

    //! Clears a depth-stencil view.
    /*!
     \param texture The texture that the depth-stencil view is associated with.
            This must be a valid texture with a depth-stencil attachment, which
            format may be used to resolve the depth and stencil values if the
            texture descriptor specifies so with the `use_clear_value` flag.
     \param dsv A native view wrapper for the depth-stencil view to clear. Must
            be convertible to an integer holding the CPU address of the view
            descriptor.
     \param clear_flags The flags indicating what to clear (depth, stencil, or
            both).
     \param depth The depth value to clear to. Ignored if \p clear_flags does
            not include `ClearFlags::kDepth`.
     \param stencil The stencil value to clear to. Ignored if \p clear_flags
            does not include `ClearFlags::kStencil`.
     */
    virtual void ClearDepthStencilView(
        const graphics::Texture& texture, const NativeObject& dsv,
        ClearFlags clear_flags,
        float depth, uint8_t stencil)
        = 0;

    //! Clears color and depth/stencil (DSV) attachments of the specified
    //! framebuffer.
    /*!
     For each color attachment, if a clear value is provided in \p
     color_clear_values, it is used. Otherwise, if the attached texture's
     descriptor has \c use_clear_value set to true, the texture's \c clear_value
     is used. If neither is specified, a default value (typically black or zero)
     is used.

     For depth and stencil, if \p depth_clear_value or \p stencil_clear_value
     are provided, they are used. Otherwise, if the depth texture's descriptor
     has \c use_clear_value set to true, its \c clear_value is used for depth.

     \param framebuffer The framebuffer whose attachments will be cleared.
     \param color_clear_values Optional vector of per-attachment clear colors.
     Each entry corresponds to a color attachment. If not set or if an entry is
     std::nullopt, the texture's clear value is used if available.
     \param depth_clear_value Optional clear value for the depth buffer. If not
     set, the texture's clear value is used if available.
     \param stencil_clear_value Optional clear value for the stencil buffer. If
     not set, the texture's clear value is used if available.
    */
    virtual void ClearFramebuffer(
        const Framebuffer& framebuffer,
        std::optional<std::vector<std::optional<Color>>> color_clear_values = std::nullopt,
        std::optional<float> depth_clear_value = std::nullopt,
        std::optional<uint8_t> stencil_clear_value = std::nullopt)
        = 0;

    virtual void ClearTextureFloat(Texture* _t, TextureSubResourceSet sub_resources, const Color& clearColor) = 0;

    virtual void CopyBuffer(
        Buffer& dst, size_t dst_offset,
        const Buffer& src, size_t src_offset,
        size_t size)
        = 0;

    //=== Resource State Management and Barriers (Templates) ===--------------//

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
            static_assert(always_false_v<T>, "Unsupported resource type for BeginTrackingResourceState");
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
            static_assert(always_false_v<T>, "Unsupported resource type for EnableAutoMemoryBarriers");
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
            static_assert(always_false_v<T>, "Unsupported resource type for DisableAutoMemoryBarriers");
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
            static_assert(always_false_v<T>, "Unsupported resource type for RequireResourceState");
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
            static_assert(always_false_v<T>, "Unsupported resource type for RequireResourceStateFinal");
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
    [[nodiscard]] auto GetSubmissionMode() const -> SubmissionMode { return submission_mode_; }
    SubmissionMode submission_mode_ { SubmissionMode::kImmediate };

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

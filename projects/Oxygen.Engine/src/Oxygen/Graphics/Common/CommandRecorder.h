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
#include <Oxygen/Base/ObserverPtr.h>
// ReSharper disable once CppUnusedIncludeDirective - For always_false_v
#include <Oxygen/Base/VariantHelpers.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
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
class Framebuffer;
class NativeView;

class CommandRecorder {
public:
  //=== Lifecycle ===-------------------------------------------------------//

  OXGN_GFX_API CommandRecorder(std::shared_ptr<CommandList> command_list,
    observer_ptr<CommandQueue> target_queue);
  OXGN_GFX_API virtual ~CommandRecorder(); // Definition moved to .cpp

  OXYGEN_MAKE_NON_COPYABLE(CommandRecorder)
  OXYGEN_MAKE_NON_MOVABLE(CommandRecorder)

  [[nodiscard]] auto GetTargetQueue() const { return target_queue_; }

  //=== Command List Control ===--------------------------------------------//

  OXGN_GFX_API virtual auto Begin() -> void;

  //! Ends the recording session and returns the recorded command list.
  /*!
   Upon return from this method, the CommandRecorder should give away ownership
   of the command list (i.e. should not continue to hold a reference to it). It
   does not matter if the rrecording failed or succeeded.

   @return The command list, with which this CommandRecorder was constructed,
   when the recording was successful. An empty shared pointer (null) indicating
   that the recording did not produce a valid command list, and the recorder
   must give away ownership of the command list.
  */
  OXGN_GFX_API virtual auto End() -> std::shared_ptr<CommandList>;

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
  virtual auto SetPipelineState(GraphicsPipelineDesc desc) -> void = 0;

  //! Sets the compute pipeline state for subsequent dispatch calls.
  /*!
   Call this before issuing any compute dispatch commands to ensure the
   correct compute shader and state are bound. The pipeline description must
   match the resources expected by the compute shader.

   \param desc The compute pipeline state description to bind.
  */
  virtual auto SetPipelineState(ComputePipelineDesc desc) -> void = 0;

  //=== Direct Binding ===--------------------------------------------------//

  virtual auto SetGraphicsRootConstantBufferView(
    uint32_t root_parameter_index, uint64_t buffer_gpu_address) -> void
    = 0;

  virtual auto SetComputeRootConstantBufferView(
    uint32_t root_parameter_index, uint64_t buffer_gpu_address) -> void
    = 0;

  virtual auto SetGraphicsRoot32BitConstant(uint32_t root_parameter_index,
    uint32_t src_data, uint32_t dest_offset_in_32bit_values) -> void
    = 0;

  virtual auto SetComputeRoot32BitConstant(uint32_t root_parameter_index,
    uint32_t src_data, uint32_t dest_offset_in_32bit_values) -> void
    = 0;

  //=== Render State ===----------------------------------------------------//

  virtual auto SetRenderTargets(
    std::span<NativeView> rtvs, std::optional<NativeView> dsv) -> void
    = 0;

  virtual auto SetViewport(const ViewPort& viewport) -> void = 0;
  virtual auto SetScissors(const Scissors& scissors) -> void = 0;

  //=== Draw and Resource Binding Commands ===------------------------------//

  // Pure bindless - Only Draw should be used, no DrawIndexed
  virtual auto Draw(uint32_t vertex_num, uint32_t instances_num,
    uint32_t vertex_offset, uint32_t instance_offset) -> void
    = 0;

  virtual auto Dispatch(uint32_t thread_group_count_x,
    uint32_t thread_group_count_y, uint32_t thread_group_count_z) -> void
    = 0;
  virtual auto SetVertexBuffers(uint32_t num,
    const std::shared_ptr<Buffer>* vertex_buffers,
    const uint32_t* strides) const -> void
    = 0;
  virtual auto BindIndexBuffer(const Buffer& buffer, Format format) -> void = 0;

  //=== Framebuffer and Resource Operations ===-----------------------------//

  // TODO: Legacy API to be removed when render passes are implemented.
  virtual auto BindFrameBuffer(const Framebuffer& framebuffer) -> void = 0;

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
  virtual auto ClearDepthStencilView(const Texture& texture,
    const NativeView& dsv, ClearFlags clear_flags, float depth, uint8_t stencil)
    -> void
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
  virtual auto ClearFramebuffer(const Framebuffer& framebuffer,
    std::optional<std::vector<std::optional<Color>>> color_clear_values
    = std::nullopt,
    std::optional<float> depth_clear_value = std::nullopt,
    std::optional<uint8_t> stencil_clear_value = std::nullopt) -> void
    = 0;

  virtual auto CopyBuffer(Buffer& dst, size_t dst_offset, const Buffer& src,
    size_t src_offset, size_t size) -> void
    = 0;

  // Copies from a (staging) buffer into a texture region. The region(s) are
  // described by TextureUploadRegion which references a buffer offset, pitches
  // and a destination TextureSlice / TextureSubResourceSet.
  virtual auto CopyBufferToTexture(
    const Buffer& src, const TextureUploadRegion& region, Texture& dst) -> void
    = 0;

  virtual auto CopyBufferToTexture(const Buffer& src,
    std::span<const TextureUploadRegion> regions, Texture& dst) -> void
    = 0;

  //! Copies a region from one texture to another.
  /*!
   Copies texture data between two textures with optional region specifications.
   Useful for compositing, render target copies, mip generation, etc.

   \param src Source texture (must be in kCopySource state).
   \param src_slice Source region to copy from.
   \param src_subresources Source mip/array slice specification.
   \param dst Destination texture (must be in kCopyDest state).
   \param dst_slice Destination region to copy to.
   \param dst_subresources Destination mip/array slice specification.

   \note The source and destination regions should have matching dimensions
         unless the backend supports scaling during copy (check capabilities).
   \note Ensure proper resource state transitions before calling this method.
   */
  virtual auto CopyTexture(const Texture& src, const TextureSlice& src_slice,
    const TextureSubResourceSet& src_subresources, Texture& dst,
    const TextureSlice& dst_slice,
    const TextureSubResourceSet& dst_subresources) -> void
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
  auto BeginTrackingResourceState(const T& resource,
    const ResourceStates initial_state, const bool keep_initial_state = false)
    -> void
  {
    if constexpr (IsBuffer<T>) {
      DoBeginTrackingResourceState(static_cast<const Buffer&>(resource),
        initial_state, keep_initial_state);
    } else if constexpr (IsTexture<T>) {
      DoBeginTrackingResourceState(static_cast<const Texture&>(resource),
        initial_state, keep_initial_state);
    } else {
      static_assert(always_false_v<T>,
        "Unsupported resource type for BeginTrackingResourceState");
    }
  }

  template <Trackable T>
  auto EnableAutoMemoryBarriers(const T& resource) -> void
  {
    if constexpr (IsBuffer<T>) {
      DoEnableAutoMemoryBarriers(static_cast<const Buffer&>(resource));
    } else if constexpr (IsTexture<T>) {
      DoEnableAutoMemoryBarriers(static_cast<const Texture&>(resource));
    } else {
      static_assert(always_false_v<T>,
        "Unsupported resource type for EnableAutoMemoryBarriers");
    }
  }

  template <Trackable T>
  auto DisableAutoMemoryBarriers(const T& resource) -> void
  {
    if constexpr (IsBuffer<T>) {
      DoDisableAutoMemoryBarriers(static_cast<const Buffer&>(resource));
    } else if constexpr (IsTexture<T>) {
      DoDisableAutoMemoryBarriers(static_cast<const Texture&>(resource));
    } else {
      static_assert(always_false_v<T>,
        "Unsupported resource type for DisableAutoMemoryBarriers");
    }
  }

  template <Trackable T>
  auto RequireResourceState(const T& resource, const ResourceStates state)
    -> void
  {
    if constexpr (IsBuffer<T>) {
      DoRequireResourceState(static_cast<const Buffer&>(resource), state);
    } else if constexpr (IsTexture<T>) {
      DoRequireResourceState(static_cast<const Texture&>(resource), state);
    } else {
      static_assert(always_false_v<T>,
        "Unsupported resource type for RequireResourceState");
    }
  }

  template <Trackable T>
  auto RequireResourceStateFinal(const T& resource, const ResourceStates state)
    -> void
  {
    if constexpr (IsBuffer<T>) {
      DoRequireResourceStateFinal(static_cast<const Buffer&>(resource), state);
    } else if constexpr (IsTexture<T>) {
      DoRequireResourceStateFinal(static_cast<const Texture&>(resource), state);
    } else {
      static_assert(always_false_v<T>,
        "Unsupported resource type for RequireResourceStateFinal");
    }
  }

  // Process all pending barriers and execute them
  OXGN_GFX_API auto FlushBarriers() -> void;

  //! @}

  //=== Synchronization ===---------------------------------------------------//

  //! Record a queue-side signal into the recorded command stream.
  /*!
   Default implementation simply forwards the signal to the target queue by
   calling `QueueSignalCommand` on the associated `CommandQueue`. Backends may
   override this to record a backend-specific command.

   @param value The fence/timeline value to signal when the recorded stream
   reaches this command during submission.
  */
  OXGN_GFX_API virtual auto RecordQueueSignal(uint64_t value) -> void;

  /*! Record a GPU-side wait into the recorded command stream. Default
   implementation simply forwards the wait to the target queue by calling
   `QueueWaitCommand` on the associated `CommandQueue`. Backends may override
   this to record a backend-specific command.

   @param value The fence/timeline value the GPU should wait for when the
   recorded stream reaches this command during submission.
  */
  OXGN_GFX_API virtual auto RecordQueueWait(uint64_t value) -> void;

protected:
  [[nodiscard]] auto GetCommandList() const -> CommandList&
  {
    return *command_list_;
  }

  //! Executes the given collection of barriers.
  /*!
   This is a backend-specific implementation that takes a collection of
   barriers and issues the appropriate commands to the GPU to execute them.
  */
  virtual auto ExecuteBarriers(std::span<const detail::Barrier> barriers)
    -> void
    = 0;

private:
  //! @{
  //! Private non-template dispatch methods for resource state tracking and
  //! barrier management.

  OXGN_GFX_API auto DoBeginTrackingResourceState(const Buffer& resource,
    ResourceStates initial_state, bool keep_initial_state) -> void;
  OXGN_GFX_API auto DoBeginTrackingResourceState(const Texture& resource,
    ResourceStates initial_state, bool keep_initial_state) -> void;

  OXGN_GFX_API auto DoEnableAutoMemoryBarriers(const Buffer& resource) -> void;
  OXGN_GFX_API auto DoEnableAutoMemoryBarriers(const Texture& resource) -> void;

  OXGN_GFX_API auto DoDisableAutoMemoryBarriers(const Buffer& resource) -> void;
  OXGN_GFX_API auto DoDisableAutoMemoryBarriers(const Texture& resource)
    -> void;

  OXGN_GFX_API auto DoRequireResourceState(
    const Buffer& resource, ResourceStates state) -> void;
  OXGN_GFX_API auto DoRequireResourceState(
    const Texture& resource, ResourceStates state) -> void;

  OXGN_GFX_API auto DoRequireResourceStateFinal(
    const Buffer& resource, ResourceStates state) -> void;
  OXGN_GFX_API auto DoRequireResourceStateFinal(
    const Texture& resource, ResourceStates state) -> void;

  //! @}

  std::shared_ptr<CommandList> command_list_;
  observer_ptr<CommandQueue> target_queue_;

  std::unique_ptr<detail::ResourceStateTracker> resource_state_tracker_;
};

} // namespace oxygen::graphics

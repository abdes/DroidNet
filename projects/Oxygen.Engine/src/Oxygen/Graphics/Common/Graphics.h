// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen {

class Platform;
namespace platform {
  class Window;
} // namespace platform

namespace graphics {
  class DescriptorAllocator;
  struct BufferDesc;
  class Buffer;
  class CommandList;
  class CommandQueue;
  class CommandRecorder;
  struct FramebufferDesc;
  class Framebuffer;
  class IShaderByteCode;
  class QueuesStrategy;
  class ResourceRegistry;
  class Surface;
  struct TextureDesc;
  class Texture;

  namespace detail {
    class DeferredReclaimer; // TODO: this needs to become public
  } // namespace detail
} // namespace graphics

namespace imgui {
  class ImguiModule;
} // namespace imgui

//! Backend-agnostic graphics device and frame orchestrator.
/*!
 The Graphics class represents the engine's device-level entry point for the
 graphics subsystem. It owns global, device-scoped facilities (command queues,
 bindless allocator/registry, command list pool) and orchestrates renderer
 lifecycles and frame execution using coroutines.

 ### Key Features

 - Device-scoped ownership of global bindless state (descriptor allocator and
   resource registry) with backend-provided installation.
 - Command queue creation via a configurable graphics::QueuesStrategy and pooled
   command list acquisition per queue role.
 - Coroutine-driven activation and run loop (see co::LiveObject), with a
   per-frame parking lot to start rendering cycles.

 ### Usage Patterns

 - Applications instantiate a backend-derived Graphics (e.g., D3D12, Vulkan),
   then call ActivateAsync() and Run() on a separate thread.

 ### Architecture Notes

 - Graphics is a Composition root; it provides access to ObjectMetadata and
   installs a backend-agnostic Bindless component at the device level. The
   descriptor allocator is late-installed via a single-assignment setter to
   avoid virtual work in constructors and to keep backend concerns out of base
   construction.
 - Global accessors GetDescriptorAllocator() and GetResourceRegistry() expose
   device-level bindless facilities to renderers and resources.

 @warning The descriptor allocator must be installed exactly once by the backend
 after device creation; accessing bindless facilities before that is a contract
 violation and triggers debug assertions.
 @see graphics::Surface, graphics::QueuesStrategy,
      graphics::DescriptorAllocator, graphics::ResourceRegistry, co::LiveObject
*/
class Graphics : public Composition,
                 public co::LiveObject,
                 public std::enable_shared_from_this<Graphics> {
public:
  OXGN_GFX_API explicit Graphics(std::string_view name);
  OXGN_GFX_API ~Graphics() override;

  OXYGEN_MAKE_NON_COPYABLE(Graphics)
  OXYGEN_DEFAULT_MOVABLE(Graphics)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view
  {
    return GetComponent<ObjectMetadata>().GetName();
  }

  //=== Async operations ===--------------------------------------------------//

  OXGN_GFX_NDAPI auto ActivateAsync(co::TaskStarted<> started)
    -> co::Co<> override;

  OXGN_GFX_API auto Run() -> void override;

  OXGN_GFX_NDAPI auto IsRunning() const -> bool override;

  OXGN_GFX_API auto Stop() -> void override;
  OXGN_GFX_API auto Flush() -> void;

  //=== Engine frame loop interface ===---------------------------------------//

  OXGN_GFX_API auto BeginFrame(
    frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void;

  OXGN_GFX_API auto EndFrame(
    frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void;

  OXGN_GFX_API auto PresentSurfaces(
    const std::vector<std::shared_ptr<graphics::Surface>>& surfaces) -> void;

  //=== Global & pooled objects ===-----------------------------------------//

  [[nodiscard]] virtual OXGN_GFX_API auto CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    observer_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<graphics::Surface>
    = 0;

  //! Initialize command queues using the provided queue management strategy.
  /*!
  @param queue_strategy The strategy for initializing command queues.
  */
  OXGN_GFX_API virtual auto CreateCommandQueues(
    const graphics::QueuesStrategy& queue_strategy) -> void;

  OXGN_GFX_NDAPI virtual auto GetCommandQueue(
    const graphics::QueueKey& key) const
    -> observer_ptr<graphics::CommandQueue>;

  OXGN_GFX_NDAPI virtual auto GetCommandQueue(graphics::QueueRole role) const
    -> observer_ptr<graphics::CommandQueue>;

  OXGN_GFX_NDAPI virtual auto FlushCommandQueues() -> void;

  OXGN_GFX_NDAPI virtual auto AcquireCommandRecorder(
    const graphics::QueueKey& queue_key, std::string_view command_list_name,
    bool immediate_submission = true)
    -> std::unique_ptr<graphics::CommandRecorder,
      std::function<void(graphics::CommandRecorder*)>>;

  // Submit any command lists that were recorded with deferred submission. This
  // will submit them to their associated target queues and call OnSubmitted()
  // on each command list after successful submission.
  OXGN_GFX_NDAPI auto SubmitDeferredCommandLists() -> void;

  OXGN_GFX_NDAPI auto AcquireCommandList(
    graphics::QueueRole queue_role, std::string_view command_list_name)
    -> std::shared_ptr<graphics::CommandList>;

  [[nodiscard]] virtual OXGN_GFX_API auto GetShader(
    std::string_view unique_id) const
    -> std::shared_ptr<graphics::IShaderByteCode>
    = 0;

  // Bindless global accessors (device-owned)
  virtual auto GetDescriptorAllocator() const
    -> const graphics::DescriptorAllocator& = 0;
  OXGN_GFX_NDAPI auto GetDescriptorAllocator()
    -> graphics::DescriptorAllocator&;

  OXGN_GFX_NDAPI auto GetResourceRegistry() const
    -> const graphics::ResourceRegistry&;
  OXGN_GFX_NDAPI auto GetResourceRegistry() -> graphics::ResourceRegistry&;

  OXGN_GFX_NDAPI auto GetDeferredReclaimer()
    -> graphics::detail::DeferredReclaimer&;

  //=== Rendering Resources factories ===-----------------------------------//

  OXGN_GFX_NDAPI auto CreateFramebuffer(const graphics::FramebufferDesc& desc)
    -> std::shared_ptr<graphics::Framebuffer>;

  [[nodiscard]] virtual auto CreateTexture(
    const graphics::TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture>
    = 0;

  [[nodiscard]] virtual auto CreateTextureFromNativeObject(
    const graphics::TextureDesc& desc,
    const graphics::NativeResource& native) const
    -> std::shared_ptr<graphics::Texture>
    = 0;

  [[nodiscard]] virtual auto CreateBuffer(
    const graphics::BufferDesc& desc) const -> std::shared_ptr<graphics::Buffer>
    = 0;

protected:
  //! Create a command queue for the given role and allocation preference.
  /*!
   Backend hook to construct an API-specific command queue mapped from the
   engine role and allocation preference.

  @param queue_name Debug name for the queue.
  @param role Engine queue role (graphics, compute, copy, etc.).
  @return Shared pointer to the created command queue.
  @throw std::runtime_error If the command queue cannot be created.

   Note: Typical callers use CreateCommandQueues() and GetCommandQueue(); this
   method is for backend implementations.
  */
  [[nodiscard]] virtual auto CreateCommandQueue(
    const graphics::QueueKey& queue_name, graphics::QueueRole role)
    -> std::shared_ptr<graphics::CommandQueue>
    = 0;

  //! Create a new command list for the given queue role (pool support).
  /*!
   Internal factory used by the command list pool. The returned command list
   must be compatible with the specified queue role.

  @param role Queue role this command list will execute on.
  @param command_list_name Debug name for the command list.
  @return Newly created command list.
  */
  [[nodiscard]] virtual auto CreateCommandListImpl(
    graphics::QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList>
    = 0;

  [[nodiscard]] virtual auto CreateCommandRecorder(
    std::shared_ptr<graphics::CommandList> command_list,
    observer_ptr<graphics::CommandQueue> target_queue)
    -> std::unique_ptr<graphics::CommandRecorder>
    = 0;

  [[nodiscard]] auto Nursery() const -> co::Nursery&
  {
    DCHECK_NOTNULL_F(nursery_);
    return *nursery_;
  }

private:
  //! The platform abstraction layer. Provided by the upper layers (e.g., the
  //! application layer).
  std::shared_ptr<Platform> platform_;

  //! The nursery for running graphics related coroutines.
  co::Nursery* nursery_ { nullptr };
};

} // namespace oxygen

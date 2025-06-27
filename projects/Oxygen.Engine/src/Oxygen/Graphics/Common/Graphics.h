// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ParkingLot.h>

namespace oxygen {

class Platform;
namespace platform {
  class Window;
} // namespace platform

namespace graphics {
  struct BufferDesc;
  class Buffer;
  class CommandList;
  class CommandQueue;
  struct FramebufferDesc;
  class Framebuffer;
  class IShaderByteCode;
  class NativeObject;
  class QueueStrategy;
  class RenderController;
  class Surface;
  struct TextureDesc;
  class Texture;

  namespace detail {
    class RenderThread;
  } // namespace detail
} // namespace graphics

namespace imgui {
  class ImguiModule;
} // namespace imgui

class Graphics : public Composition, public co::LiveObject {
public:
  OXYGEN_GFX_API explicit Graphics(std::string_view name);
  OXYGEN_GFX_API ~Graphics() override;

  OXYGEN_MAKE_NON_COPYABLE(Graphics)
  OXYGEN_DEFAULT_MOVABLE(Graphics)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view
  {
    return GetComponent<ObjectMetaData>().GetName();
  }

  //=== Async operations ===------------------------------------------------//

  [[nodiscard]] OXYGEN_GFX_API auto ActivateAsync(co::TaskStarted<> started)
    -> co::Co<> override;

  OXYGEN_GFX_API auto Run() -> void override;

  [[nodiscard]] OXYGEN_GFX_API auto IsRunning() const -> bool override;

  OXYGEN_GFX_API auto Stop() -> void override;

  auto OnRenderStart() { return render_.Park(); }

  auto Render() -> void { render_.UnParkAll(); }

  //=== Global & pooled objects ===-----------------------------------------//

  [[nodiscard]] virtual OXYGEN_GFX_API auto CreateRenderController(
    std::string_view name, std::weak_ptr<graphics::Surface> surface,
    uint32_t frames_in_flight) -> std::shared_ptr<graphics::RenderController>;

  [[nodiscard]] virtual OXYGEN_GFX_API auto CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    std::shared_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<graphics::Surface>
    = 0;

  //! Initialize command queues using the provided queue management strategy.
  /*!
   \param queue_strategy The strategy for initializing command queues.
  */
  OXYGEN_GFX_API auto CreateCommandQueues(
    const graphics::QueueStrategy& queue_strategy) -> void;

  [[nodiscard]] OXYGEN_GFX_API auto GetCommandQueue(std::string_view name) const
    -> std::shared_ptr<graphics::CommandQueue>;

  OXYGEN_GFX_API auto FlushCommandQueues() -> void;

  [[nodiscard]] OXYGEN_GFX_API auto AcquireCommandList(
    graphics::QueueRole queue_role, std::string_view command_list_name)
    -> std::shared_ptr<graphics::CommandList>;

  [[nodiscard]] virtual OXYGEN_GFX_API auto GetShader(
    std::string_view unique_id) const
    -> std::shared_ptr<graphics::IShaderByteCode>
    = 0;

  //=== Rendering Resources factories ===-----------------------------------//

  [[nodiscard]] virtual auto CreateFramebuffer(
    const graphics::FramebufferDesc& desc, graphics::RenderController& renderer)
    -> std::shared_ptr<graphics::Framebuffer>
    = 0;

  [[nodiscard]] virtual auto CreateTexture(
    const graphics::TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture>
    = 0;

  [[nodiscard]] virtual auto CreateTextureFromNativeObject(
    const graphics::TextureDesc& desc,
    const graphics::NativeObject& native) const
    -> std::shared_ptr<graphics::Texture>
    = 0;

  [[nodiscard]] virtual auto CreateBuffer(
    const graphics::BufferDesc& desc) const -> std::shared_ptr<graphics::Buffer>
    = 0;

protected:
  //! Create a command queue for the given role and allocation preference.
  /*!
   \param queue_name The debug name for this queue.
   \param role The role of the command queue.
   \param allocation_preference The allocation preference for the command queue.
   \return A shared pointer to the created command queue.
   \throw std::runtime_error If the command queue could not be created.

   This method is called by the graphics backend to create a command queue for
   a specific role and allocation preference. The backend implementation is
   responsible for mapping these parameters to API-specific queue types or
   families.
  */
  [[nodiscard]] virtual auto CreateCommandQueue(std::string_view queue_name,
    graphics::QueueRole role,
    graphics::QueueAllocationPreference allocation_preference)
    -> std::shared_ptr<graphics::CommandQueue>
    = 0;

  /**
   * Creates a new command list for the given queue role.
   * For internal use by the command list pool.
   */
  [[nodiscard]] virtual auto CreateCommandListImpl(
    graphics::QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList>
    = 0;

  [[nodiscard]] auto Nursery() const -> co::Nursery&
  {
    DCHECK_NOTNULL_F(nursery_);
    return *nursery_;
  }

  [[nodiscard]] virtual auto CreateRendererImpl(std::string_view name,
    std::weak_ptr<graphics::Surface> surface, uint32_t frames_in_flight)
    -> std::unique_ptr<graphics::RenderController>
    = 0;

private:
  //! The platform abstraction layer. Provided by the upper layers (e.g., the
  //! application layer).
  std::shared_ptr<Platform> platform_;

  using CommandQueueSharedPtr = std::shared_ptr<graphics::CommandQueue>;
  //! The command queues created by the backend.
  std::unordered_map<std::string, CommandQueueSharedPtr> command_queues_;

  using CommandListUniquePtr = std::unique_ptr<graphics::CommandList>;
  using CommandLists = std::vector<CommandListUniquePtr>;
  //! Pool of available command lists by queue type.
  std::unordered_map<graphics::QueueRole, CommandLists> command_list_pool_;
  std::mutex command_list_pool_mutex_;

  using RendererWeakPtr = std::weak_ptr<graphics::RenderController>;
  //! Active renderers managed by this Graphics instance.
  /*!
   We consider that the RenderingController is created and owned by the upper
   layers (e.g., the application layer). Its lifetime is tied to the lifetime
   of the application and the associated rendering context. At the graphics
   level, we only care about the renderers while they are still alive. When
   they are not, we just forget about them.
  */
  std::vector<RendererWeakPtr> renderers_;

  //! The nursery for running graphics related coroutines.
  co::Nursery* nursery_ { nullptr };
  //! A synchronization parking lot for rendering controllers to wait for the
  //! start of the next frame rendering cycle.
  co::ParkingLot render_ {};
};

} // namespace oxygen

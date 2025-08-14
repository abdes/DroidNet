//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>

namespace oxygen {
namespace platform {
  class Platform;
  class Window;
} // namespace platform
class Graphics;
namespace graphics {
  class CommandRecorder;
  class RenderTarget;
  class RenderController;
  class Surface;
  class Framebuffer;
  class Buffer;
} // namespace graphics
namespace co {
  class Nursery;
} // namespace co
namespace engine {
  class Renderer;
} // namespace engine
} // namespace oxygen

namespace oxygen::examples {

class MainModule final {
public:
  MainModule(
    std::shared_ptr<Platform> platform, std::weak_ptr<Graphics> gfx_weak);
  ~MainModule();

  OXYGEN_MAKE_NON_COPYABLE(MainModule)
  OXYGEN_MAKE_NON_MOVABLE(MainModule)

  auto StartAsync(co::TaskStarted<> started = {}) -> co::Co<>
  {
    return OpenNursery(nursery_, std::move(started));
  }

  auto Run() -> void;

private:
  // Use engine::SceneConstants (world matrix removed; per-item transform
  // pending later phase)
  oxygen::engine::SceneConstants scene_constants_ {};

  // Use engine::MaterialConstants through renderer snapshot API (no local
  // buffer management) Legacy local struct removed.

  auto SetupCommandQueues() const -> void;
  auto SetupMainWindow() -> void;
  auto SetupSurface() -> void;
  auto SetupRenderer() -> void;
  auto SetupFramebuffers() -> void;
  auto SetupShaders() const -> void;

  auto RenderScene() -> co::Co<>;

  std::shared_ptr<Platform> platform_;
  std::weak_ptr<Graphics> gfx_weak_;
  std::weak_ptr<platform::Window> window_weak_;
  std::shared_ptr<graphics::Surface> surface_;
  std::shared_ptr<graphics::RenderController> render_controller_;
  std::shared_ptr<engine::Renderer> renderer_;
  StaticVector<std::shared_ptr<graphics::Framebuffer>, kFrameBufferCount>
    framebuffers_ {};
  engine::RenderContext context_ {};

  std::shared_ptr<graphics::Buffer> scene_constants_buffer_;
  // Removed: bindless indices buffer now managed by Renderer.
  // Helper method to translate asset to engine::MaterialConstants
  auto ExtractMaterialConstants(const data::MaterialAsset& material) const
    -> engine::MaterialConstants;

  // Render items now managed by engine::Renderer (Phase 3)

  co::Nursery* nursery_ { nullptr };
  // Removed: EnsureBindlessIndexingBuffer (Renderer now owns indices upload).
  // Phase 2: SRVs and indices handled by Renderer; no per-example SRV state.
};

} // namespace oxygen::examples

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
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>

namespace oxygen {
class Platform;
namespace platform {
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
  auto SetupCommandQueues() const -> void;
  auto SetupMainWindow() -> void;
  auto SetupSurface() -> void;
  auto SetupRenderer() -> void;
  auto SetupFramebuffers() -> void;
  auto SetupRenderPasses() -> void;

  auto RenderScene() -> co::Co<>;

  std::shared_ptr<Platform> platform_;
  std::weak_ptr<Graphics> gfx_weak_;
  std::weak_ptr<platform::Window> window_weak_;
  std::shared_ptr<graphics::Surface> surface_;
  std::shared_ptr<graphics::RenderController> renderer_;
  StaticVector<std::shared_ptr<graphics::Framebuffer>, kFarmesInFlight>
    framebuffers_ {};

  std::shared_ptr<graphics::Buffer> constant_buffer_;
  graphics::NativeObject index_mapping_cbv_ {};

  co::Nursery* nursery_ { nullptr };
  float rotation_angle_ { 0.0f };

  std::vector<engine::RenderItem> render_items_;
  std::shared_ptr<engine::DepthPrePassConfig> depth_pre_pass_config_;
  std::unique_ptr<engine::DepthPrePass> depth_pre_pass_;
};

} // namespace oxygen::examples

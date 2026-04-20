//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/Runtime/AppWindow.h"

namespace oxygen {
namespace engine {
  class FrameContext;
}
namespace graphics {
  class Framebuffer;
  class Surface;
}
namespace vortex {
  class Renderer;
}
} // namespace oxygen

namespace oxygen::examples {

class DemoAppContext;

//! Base class for demo engine modules.
/*!
  Implements shared helpers and storage for common demo lifecycle pieces such
  as the main window, view publication, runtime composition, and demo shell
  integration on top of the Vortex renderer.
*/
class DemoModuleBase : public engine::EngineModule, public Composition {
  OXYGEN_TYPED(DemoModuleBase)
public:
  explicit DemoModuleBase(const DemoAppContext& app) noexcept;
  ~DemoModuleBase() override;

  OXYGEN_MAKE_NON_COPYABLE(DemoModuleBase)
  OXYGEN_MAKE_NON_MOVABLE(DemoModuleBase)

  auto OnAttached(observer_ptr<IAsyncEngine> engine) noexcept -> bool final;
  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(observer_ptr<engine::FrameContext> context)
    -> void override;
  auto OnSceneMutation(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnPreRender(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;
  auto OnCompositing(observer_ptr<engine::FrameContext> context)
    -> co::Co<> final;
  auto OnPublishViews(observer_ptr<engine::FrameContext> context)
    -> co::Co<> override;

protected:
  //! Hook: derived demos create and configure the DemoShell instance.
  virtual auto OnAttachedImpl(observer_ptr<IAsyncEngine> engine) noexcept
    -> std::unique_ptr<DemoShell>
    = 0;

  //! Access the owned DemoShell instance (must be initialized).
  auto GetShell() -> DemoShell&;

  //! Hook: allow derived demos to customize window properties.
  virtual auto BuildDefaultWindowProperties() const
    -> platform::window::Properties;

  //! Hook retained for existing demos; Vortex runtime path does not require
  //! explicit backbuffer cleanup and most implementations can now no-op.
  virtual auto ClearBackbufferReferences() -> void = 0;

  //! Hook: derived classes fill this with the views they want to render this
  //! frame.
  virtual auto UpdateComposition(engine::FrameContext& /*context*/,
    std::vector<renderer::CompositionView>& /*views*/) -> void
  {
  }

  // Helpers
  auto GetOrCreateViewId(std::string_view name) -> ViewId;
  auto ClearViewIds() -> void;

  //! Resolve the active Vortex renderer module if available.
  auto ResolveVortexRenderer() const noexcept -> observer_ptr<vortex::Renderer>;

  // State
  const DemoAppContext& app_;
  observer_ptr<AppWindow> app_window_ { nullptr };

  //! Map of logical view names to persistent ViewIds for resource tracking.
  std::map<std::string, ViewId> view_registry_;
  //! The active descriptors for the current frame.
  std::vector<renderer::CompositionView> active_views_;

private:
  struct RuntimeSceneTarget {
    std::shared_ptr<graphics::Framebuffer> framebuffer {};
    uint32_t width { 0 };
    uint32_t height { 0 };
  };

  std::unique_ptr<DemoShell> shell_;

  auto OnFrameStartCommon(engine::FrameContext& context) -> void;
  auto EnsureSceneFramebuffer(ViewId view_id, uint32_t width, uint32_t height)
    -> std::shared_ptr<graphics::Framebuffer>;
  auto ClearSceneFramebuffers() -> void;
  auto ReleaseInactiveRuntimeViews(observer_ptr<engine::FrameContext> context,
    const std::vector<ViewId>& retained_intent_ids) -> void;

  observer_ptr<graphics::Surface> last_surface_ { nullptr };
  std::map<ViewId, RuntimeSceneTarget> scene_targets_;
};

} // namespace oxygen::examples

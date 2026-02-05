//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <map>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Window.h>

#include "DemoShell/Runtime/AppWindow.h"

#include "DemoShell/Runtime/CompositionView.h"

namespace oxygen {
class AsyncEngine;
namespace engine {
  class FrameContext;
}
namespace graphics {
  class Surface;
}
} // namespace oxygen

namespace oxygen::examples {

class DemoAppContext;
class RenderingPipeline;

//! Base class for demo engine modules.
/*!
  Implements shared helpers and storage for common demo lifecycle pieces such
  as the main window, window controller and render lifecycle helper.
*/
class DemoModuleBase : public engine::EngineModule, public Composition {
  OXYGEN_TYPED(DemoModuleBase)
public:
  explicit DemoModuleBase(const DemoAppContext& app) noexcept;

  ~DemoModuleBase() override;

  auto OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool override;
  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;

protected:
  //! Hook: allow derived demos to customize window properties.
  virtual auto BuildDefaultWindowProperties() const
    -> platform::window::Properties;

  //! Hook: clear backbuffer references before resize.
  virtual auto ClearBackbufferReferences() -> void = 0;

  // Example specific hooks
  virtual auto HandleOnFrameStart(engine::FrameContext& /*context*/) -> void { }

  //! Hook: derived classes fill this with the views they want to render this
  //! frame.
  virtual auto UpdateComposition(engine::FrameContext& /*context*/,
    std::vector<CompositionView>& /*views*/) -> void
  {
  }

  // Helpers
  auto GetOrCreateViewId(std::string_view name) -> ViewId;
  auto ClearViewIds() -> void;

  // State
  const DemoAppContext& app_;
  observer_ptr<AppWindow> app_window_ { nullptr };
  std::unique_ptr<RenderingPipeline> pipeline_;

  //! Map of logical view names to persistent ViewIds for resource tracking.
  std::map<std::string, ViewId> view_registry_;
  //! The active descriptors for the current frame.
  std::vector<CompositionView> active_views_;

private:
  auto OnFrameStartCommon(engine::FrameContext& context) -> void;
  auto MarkSurfacePresentable(engine::FrameContext& context) -> void;

  observer_ptr<graphics::Surface> last_surface_ { nullptr };
};

} // namespace oxygen::examples

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Platform/Window.h>

#include "DemoShell/Runtime/AppWindow.h"

namespace oxygen::examples {

class DemoAppContext;

//! Base class for demo engine modules.
/*!
 Implements shared helpers and storage for common demo lifecycle pieces such
 as the main window, window controller and render lifecycle helper.

 Derived demo modules should either call the helper methods provided by this
 base from their OnAttached() handler (recommended) or rely on the base to
 perform common setup. The base will add per-window components during
 construction so demos receive a fully configured Composition during
 OnAttached.
*/
class DemoModuleBase : public oxygen::engine::EngineModule,
                       public oxygen::Composition {
  OXYGEN_TYPED(DemoModuleBase)
public:
  explicit DemoModuleBase(const DemoAppContext& app) noexcept;

  ~DemoModuleBase() override = default;

  // Lifecycle: create window and install helpers when attached to engine.
  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;

  // Common OnFrameStart handler. Derived demos should implement
  // OnDemoFrameStart to provide per-demo behavior (scene setup,
  // context.SetScene, etc.). The base handles shared lifecycle tasks such
  // as handling expired windows, resize flow, surface registration and
  // ImGui window assignment.
  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;

protected:
  //! Hook: allow derived demos to customize window properties. Default
  //! implementation returns reasonable defaults. Derived classes should
  //! override to tune title, size, flags.
  virtual auto BuildDefaultWindowProperties() const
    -> platform::window::Properties;

  auto MarkSurfacePresentable(engine::FrameContext& context) -> void;

  //! Hook: clear backbuffer references before resize. Each demo must
  //! implement this to clear any texture references that point to the
  //! backbuffer before it is resized/recreated. Typical references come from
  //! the render graph.
  virtual auto ClearBackbufferReferences() -> void = 0;

  // Reference to the shared demo App state (must outlive the module).
  const DemoAppContext& app_;

  // Per-window helpers.

  // This module is itself a Composition so it can own demo components
  // directly (AddComponent is protected in Composition, deriving allows us
  // to construct components here).
  oxygen::observer_ptr<AppWindow> app_window_ { nullptr };

  // Hook called by the base OnFrameStart so derived demos only implement
  // the app-specific parts (scene setup, context.SetScene, etc.). Default
  // implementation is a no-op.
  virtual auto OnExampleFrameStart(engine::FrameContext& /*context*/) -> void {
  }

private:
  auto OnFrameStartCommon(engine::FrameContext& context) -> void;

  oxygen::observer_ptr<graphics::Surface> last_surface_ { nullptr };
};

} // namespace oxygen::examples

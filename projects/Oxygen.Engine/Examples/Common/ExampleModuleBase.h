//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "AppWindow.h"
#include "RenderGraph.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Platform/Window.h>

namespace oxygen::examples::common {

struct AsyncEngineApp;

//! Base class for example engine modules.
/*! Implements shared helpers and storage for common example lifecycle pieces
    such as the main window, window controller and render lifecycle helper.

    Derived example modules should either call the helper methods provided
    by this base from their OnAttached() handler (recommended) or rely on
    the base to perform common setup. The base will add per-window components
    during construction so examples receive a fully configured Composition
    during OnAttached.
*/
class ExampleModuleBase : public oxygen::engine::EngineModule,
                          public oxygen::Composition {
  OXYGEN_TYPED(ExampleModuleBase)
public:
  explicit ExampleModuleBase(
    const oxygen::examples::common::AsyncEngineApp& app) noexcept;
  // Lifecycle: create window and install helpers when attached to engine
  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  // Common OnFrameStart handler. Derived examples should implement
  // OnExampleFrameStart to provide per-example behavior (scene setup,
  // context.SetScene, etc.). The base handles shared lifecycle tasks such
  // as handling expired windows, resize flow, surface registration and
  // ImGui window assignment.
  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  ~ExampleModuleBase() override = default;

  //! Install helpers for an existing window using the aggregated app state
  //! provided by examples. Returns true on success.
  // Installation of per-window components is performed by OnAttached, which
  // adds the AppWindow component for a single, combined window + render
  // lifecycle owner. Derived classes should rely on the base
  // OnAttached behaviour instead of calling an explicit install helper.

  //! Hook: allow derived examples to customize window properties. Default
  //! implementation returns reasonable defaults. Derived classes should
  //! override to tune title, size, flags.
  virtual auto BuildDefaultWindowProperties() const
    -> platform::window::Properties;

protected:
  // Reference to the shared example App state (must outlive the module)
  const oxygen::examples::common::AsyncEngineApp& app_;

  // Per-window helpers

  // This module is itself a Composition so it can own example components
  // directly (AddComponent is protected in Composition, deriving allows us
  // to construct components here).
  oxygen::observer_ptr<AppWindow> app_window_ { nullptr };

  // Example-local render-graph helper component.
  oxygen::observer_ptr<RenderGraph> render_graph_ { nullptr };

  // Hook called by the base OnFrameStart so derived examples only implement
  // the app-specific parts (scene setup, context.SetScene, etc.). Default
  // implementation is a no-op.
  virtual auto OnExampleFrameStart(engine::FrameContext& /*context*/) -> void {
  }

  // Note: this base no longer stores per-window surface / framebuffer
  // state. The `AppWindow` component owns the window, surface and
  // framebuffer data and behaviour — the base delegates via the
  // delegates via component APIs.
};

} // namespace oxygen::examples::common

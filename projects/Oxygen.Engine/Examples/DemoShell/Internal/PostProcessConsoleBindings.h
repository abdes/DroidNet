//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/Command.h>

namespace oxygen::console {
class Console;
}
namespace oxygen::vortex {
class Renderer;
}
namespace oxygen::examples::ui {
class PostProcessSettingsService;
}

namespace oxygen::examples::internal {

//! Engine-thread bindings for the DemoShell scene owner and published exposure
//! owners.
class PostProcessConsoleBindings final {
public:
  using RendererResolver = std::function<observer_ptr<vortex::Renderer>()>;
  PostProcessConsoleBindings(console::Console& console,
    ui::PostProcessSettingsService& settings, RendererResolver renderer);
  ~PostProcessConsoleBindings();
  OXYGEN_MAKE_NON_COPYABLE(PostProcessConsoleBindings)
  OXYGEN_MAKE_NON_MOVABLE(PostProcessConsoleBindings)

private:
  using Arguments = std::vector<std::string>;
  using Result = console::ExecutionResult;
  auto Unregister() -> void;
  [[nodiscard]] auto HasTarget(const Arguments& args) const -> bool;
  [[nodiscard]] auto Accepted() const -> Result;
  auto Targets(const Arguments& args) -> Result;
  auto Inspect(const Arguments& args) -> Result;
  auto Exposure(const Arguments& args) -> Result;
  auto Camera(const Arguments& args) -> Result;
  auto Output(const Arguments& args) -> Result;
  auto Curve(const Arguments& args) -> Result;
  auto Transition(const Arguments& args) -> Result;
  auto TransitionStatus(const Arguments& args) -> Result;

  console::Console& console_;
  ui::PostProcessSettingsService& settings_;
  RendererResolver renderer_;
  std::vector<console::CommandHandle> handles_;
};

} // namespace oxygen::examples::internal

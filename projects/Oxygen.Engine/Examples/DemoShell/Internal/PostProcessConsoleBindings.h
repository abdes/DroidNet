//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/CommandRegistry.h>

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
  using Args = commands::CommandArgs;
  using Result = commands::CommandResult;
  auto Register(commands::CommandDefinition definition) -> void;
  auto Unregister() -> void;
  //! True when `target` names the current authoring target, `scene:<rev>`.
  [[nodiscard]] auto IsCurrentTarget(std::string_view target) const -> bool;
  [[nodiscard]] auto Accepted() const -> Result;
  auto Targets() -> Result;
  auto Inspect(const Args& args) -> Result;
  auto Exposure(const Args& args) -> Result;
  auto Camera(const Args& args) -> Result;
  auto Output(const Args& args) -> Result;
  auto Curve(const Args& args) -> Result;
  auto ResetAuto(const Args& args) -> Result;
  auto Transition(const Args& args) -> Result;
  auto TransitionStatus(const Args& args) -> Result;

  console::Console& console_;
  ui::PostProcessSettingsService& settings_;
  RendererResolver renderer_;
  std::vector<commands::CommandHandle> handles_;
};

} // namespace oxygen::examples::internal

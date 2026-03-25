//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

struct VsmPageFlagPropagationPassConfig {
  std::string debug_name { "VsmPageFlagPropagationPass" };
};

class VsmPageFlagPropagationPass : public RenderPass {
public:
  using Config = VsmPageFlagPropagationPassConfig;

  OXGN_RNDR_API VsmPageFlagPropagationPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmPageFlagPropagationPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmPageFlagPropagationPass)
  OXYGEN_DEFAULT_MOVABLE(VsmPageFlagPropagationPass)

  OXGN_RNDR_API auto SetFrameInput(renderer::vsm::VsmPageAllocationFrame frame)
    -> void;
  OXGN_RNDR_API auto ResetFrameInput() noexcept -> void;

protected:
  auto ValidateConfig() -> void override;
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto OnPrepareResources(graphics::CommandRecorder& recorder) -> void override;
  auto OnExecute(graphics::CommandRecorder& recorder) -> void override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::engine

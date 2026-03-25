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
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

struct VsmPageInitializationPassConfig {
  std::string debug_name { "VsmPageInitializationPass" };
};

struct VsmPageInitializationPassInput {
  renderer::vsm::VsmPageAllocationFrame frame {};
  renderer::vsm::VsmPhysicalPoolSnapshot physical_pool {};
};

class VsmPageInitializationPass : public RenderPass {
public:
  using Config = VsmPageInitializationPassConfig;

  OXGN_RNDR_API VsmPageInitializationPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmPageInitializationPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmPageInitializationPass)
  OXYGEN_DEFAULT_MOVABLE(VsmPageInitializationPass)

  OXGN_RNDR_API auto SetInput(VsmPageInitializationPassInput input) -> void;
  OXGN_RNDR_API auto ResetInput() noexcept -> void;

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

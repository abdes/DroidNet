//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/Passes/Detail/VirtualShadowPassScaffolding.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen {
class Graphics;
}

namespace oxygen::engine {

struct VirtualShadowBuildClearArgsPassConfig {
  std::string debug_name { "VirtualShadowBuildClearArgsPass" };
};

class VirtualShadowBuildClearArgsPass : public ComputeRenderPass {
public:
  using Config = VirtualShadowBuildClearArgsPassConfig;

  OXGN_RNDR_API VirtualShadowBuildClearArgsPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VirtualShadowBuildClearArgsPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowBuildClearArgsPass)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowBuildClearArgsPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  static constexpr std::uint32_t kPassConstantsSlotsPerFrame = 1U;
  static constexpr std::uint32_t kPassConstantsSlotCount
    = kPassConstantsSlotsPerFrame * frame::kFramesInFlight.get();

  observer_ptr<Graphics> gfx_;
  std::shared_ptr<Config> config_;

  detail::VirtualShadowPassConstantBufferOwner<kPassConstantsSlotCount>
    pass_constants_;

  ViewId active_view_id_ {};
  bool active_dispatch_ { false };
};

} // namespace oxygen::engine

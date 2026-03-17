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
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/Passes/Detail/VirtualShadowPassScaffolding.h>
#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen {
class Graphics;
}

namespace oxygen::engine {

struct VirtualShadowClearPassConfig {
  std::string debug_name { "VirtualShadowClearPass" };
};

class VirtualShadowClearPass : public ComputeRenderPass {
public:
  using Config = VirtualShadowClearPassConfig;

  OXGN_RNDR_API VirtualShadowClearPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VirtualShadowClearPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowClearPass)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowClearPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  static constexpr std::uint32_t kDispatchGroupSize = 64U;
  static constexpr std::uint32_t kPassConstantsSlotsPerFrame = 1U;
  static constexpr std::uint32_t kPassConstantsSlotCount
    = kPassConstantsSlotsPerFrame * frame::kFramesInFlight.get();

  observer_ptr<Graphics> gfx_;
  std::shared_ptr<Config> config_;

  detail::VirtualShadowPassConstantBufferOwner<kPassConstantsSlotCount>
    pass_constants_;

  ViewId active_view_id_ {};
  renderer::VirtualShadowPageManagementBindings active_page_management_bindings_ {};
  std::uint32_t active_total_page_count_ { 0U };
  std::uint32_t active_dispatch_group_count_ { 0U };
  bool active_dispatch_ { false };
};

} // namespace oxygen::engine

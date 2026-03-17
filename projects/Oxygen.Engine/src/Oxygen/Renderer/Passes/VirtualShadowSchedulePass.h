//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/Passes/Detail/VirtualShadowPassScaffolding.h>
#include <Oxygen/Renderer/Passes/Detail/VirtualShadowScheduleResources.h>
#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen {
class Graphics;
}

namespace oxygen::engine {

struct VirtualShadowSchedulePassConfig {
  std::string debug_name { "VirtualShadowSchedulePass" };
};

class VirtualShadowSchedulePass : public ComputeRenderPass {
public:
  using Config = VirtualShadowSchedulePassConfig;

  OXGN_RNDR_API VirtualShadowSchedulePass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VirtualShadowSchedulePass() override;

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowSchedulePass)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowSchedulePass)

  [[nodiscard]] OXGN_RNDR_NDAPI auto HasActiveDispatch() const noexcept -> bool
  {
    return active_dispatch_;
  }

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetActiveDrawCount() const noexcept
    -> std::uint32_t
  {
    return active_draw_count_;
  }

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetActiveDrawBoundsSrv() const noexcept
    -> ShaderVisibleIndex
  {
    return active_draw_bounds_srv_;
  }

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetScheduleResources() const noexcept
    -> const detail::VirtualShadowScheduleResources*
  {
    const auto it = view_schedule_resources_.find(active_view_id_);
    return it != view_schedule_resources_.end() ? &it->second : nullptr;
  }

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

  std::shared_ptr<graphics::Buffer> clear_count_upload_buffer_;
  void* clear_count_upload_mapped_ptr_ { nullptr };

  std::unordered_map<ViewId, detail::VirtualShadowScheduleResources>
    view_schedule_resources_;

  ViewId active_view_id_ {};
  ShaderVisibleIndex active_draw_bounds_srv_ { kInvalidShaderVisibleIndex };
  renderer::VirtualShadowPageManagementBindings active_page_management_bindings_ {};
  std::uint32_t active_total_page_count_ { 0U };
  std::uint32_t active_dispatch_group_count_ { 0U };
  std::uint32_t active_draw_count_ { 0U };
  bool active_dispatch_ { false };
  OXGN_RNDR_API auto EnsureClearCountUploadBuffer() -> void;
  OXGN_RNDR_API auto EnsureViewScheduleResources(ViewId view_id,
    std::uint32_t required_entry_capacity, std::uint32_t required_draw_count)
    -> detail::VirtualShadowScheduleResources*;
};

} // namespace oxygen::engine

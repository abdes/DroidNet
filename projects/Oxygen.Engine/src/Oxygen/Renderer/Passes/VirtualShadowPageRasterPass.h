//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>

namespace oxygen::engine {

class VirtualShadowPageRasterPass : public DepthPrePass {
public:
  using Config = DepthPrePassConfig;

  OXGN_RNDR_API explicit VirtualShadowPageRasterPass(
    std::shared_ptr<Config> config);
  ~VirtualShadowPageRasterPass() override;

  OXYGEN_DEFAULT_COPYABLE(VirtualShadowPageRasterPass)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowPageRasterPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto UsesFramebufferDepthAttachment() const -> bool override;
  auto BuildRasterizerStateDesc(graphics::CullMode cull_mode) const
    -> graphics::RasterizerStateDesc override;

private:
  auto PrepareFullAtlasDepthStencilView(graphics::Texture& depth_texture) const
    -> graphics::NativeView;
  auto EnsureShadowViewConstantsCapacity(std::uint32_t required_pages) -> void;
  auto UploadResolvedPageViewConstants(
    std::span<const renderer::VirtualShadowResolvedRasterPage> pages) -> void;
  auto BindResolvedPageViewConstants(graphics::CommandRecorder& recorder,
    std::uint32_t page_index) const -> void;
  auto SetResolvedPageViewportAndScissors(graphics::CommandRecorder& recorder,
    const renderer::VirtualShadowRenderPlan& render_plan,
    const renderer::VirtualShadowResolvedRasterPage& page) const -> void;
  auto EmitResolvedPageCulledDrawRange(graphics::CommandRecorder& recorder,
    const DrawMetadata* records, std::span<const glm::vec4> draw_bounds,
    const renderer::VirtualShadowResolvedRasterPage& page, std::uint32_t begin,
    std::uint32_t end, std::uint32_t& emitted_count,
    std::uint32_t& page_local_culled_count, std::uint32_t& skipped_invalid,
    std::uint32_t& draw_errors) const noexcept -> void;

  mutable graphics::NativeView full_atlas_dsv_ {};
  std::shared_ptr<graphics::Buffer> shadow_view_constants_buffer_;
  void* shadow_view_constants_mapped_ptr_ { nullptr };
  std::uint32_t shadow_view_constants_capacity_ { 0U };
  std::vector<ViewConstants::GpuData> resolved_page_view_constants_upload_;

  struct ViewLogState {
    bool saw_live_prepared_frame { false };
    bool saw_live_plan_jobs { false };
    bool saw_zero_draw_live_frame { false };
    bool saw_nonzero_draw_live_frame { false };
  };
  std::unordered_map<std::uint64_t, ViewLogState> view_log_states_;
};

} // namespace oxygen::engine

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <array>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>

namespace oxygen::engine {

struct DrawMetadata;

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
  auto ExtendShaderDefines(
    std::vector<graphics::ShaderDefine>& defines) const -> void override;

private:
  struct alignas(16) RasterPassConstants {
    float alpha_cutoff_default { 0.5F };
    std::uint32_t schedule_srv_index { 0xFFFFFFFFU };
    std::uint32_t schedule_count_srv_index { 0xFFFFFFFFU };
    std::uint32_t atlas_tiles_per_axis { 0U };
    std::uint32_t draw_page_ranges_srv_index { 0xFFFFFFFFU };
    std::uint32_t draw_page_indices_srv_index { 0xFFFFFFFFU };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
  };
  static_assert(sizeof(RasterPassConstants) % 16U == 0U);

  auto PrepareFullAtlasDepthStencilView(graphics::Texture& depth_texture) const
    -> graphics::NativeView;
  auto EnsureClearPipelineState() -> void;
  auto EnsurePassConstantsCapacity() -> void;
  auto HasLiveGpuRasterInputs(
    const renderer::VirtualShadowGpuRasterInputs& gpu_inputs) const -> bool;
  auto UploadRasterPassConstants() -> void;
  auto EmitIndirectResolvedPageDrawRange(graphics::CommandRecorder& recorder,
    const graphics::Buffer& draw_args_buffer, const DrawMetadata* records,
    std::uint32_t draw_count, std::uint32_t begin, std::uint32_t end,
    std::uint64_t& indirect_draw_record_count,
    std::uint32_t& cpu_draw_submission_count, std::uint32_t& skipped_invalid,
    std::uint32_t& draw_errors) const noexcept -> void;

  mutable graphics::NativeView full_atlas_dsv_ {};

  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  void* pass_constants_mapped_ptr_ { nullptr };
  std::array<graphics::NativeView, frame::kFramesInFlight.get()>
    pass_constants_cbvs_ {};
  std::array<ShaderVisibleIndex, frame::kFramesInFlight.get()>
    pass_constants_indices_ {};
  std::optional<graphics::GraphicsPipelineDesc> clear_pso_ {};

  struct ViewLogState {
    bool saw_live_prepared_frame { false };
    bool saw_live_plan_jobs { false };
    bool saw_zero_draw_live_frame { false };
    bool saw_nonzero_draw_live_frame { false };
  };
  std::unordered_map<std::uint64_t, ViewLogState> view_log_states_;
};

} // namespace oxygen::engine

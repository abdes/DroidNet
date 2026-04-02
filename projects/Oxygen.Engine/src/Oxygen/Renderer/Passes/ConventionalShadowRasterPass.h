//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/RasterShadowRenderPlan.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

namespace oxygen::graphics::detail {
class DeferredReclaimer;
}

namespace oxygen::engine {

//! Conventional raster shadow-map rendering pass.
/*!
 This pass derives from `DepthPrePass` intentionally because it is still a
 depth-only geometry raster pass over the same prepared draw metadata contract.
 It reuses the shared depth-only raster infrastructure already implemented by
 `DepthPrePass`: partition-aware opaque/masked PSO selection, depth-target
 preparation and clear, viewport/scissor setup, and robust draw submission.

 What this class changes is not the core raster model, but the shadow-specific
 execution contract: it consumes a generic raster shadow render plan, binds
 per-job shadow view constants, and targets shadow-map slices rather than the
 main-view depth buffer. It intentionally does not consume or publish the
 canonical scene `DepthPrePassOutput`, because its output lives in the shadow
 domain and must never be mistaken for the main-view depth product.
*/
class ConventionalShadowRasterPass : public DepthPrePass {
public:
  using Config = DepthPrePassConfig;

  struct IndirectPartitionInspection {
    PassMask pass_mask {};
    std::uint32_t partition_index { 0U };
    std::uint32_t draw_record_count { 0U };
    std::uint32_t max_commands_per_job { 0U };
    const graphics::Buffer* command_buffer { nullptr };
    const graphics::Buffer* count_buffer { nullptr };
  };

  struct Output {
    std::uint32_t job_count { 0U };
    std::uint32_t partition_count { 0U };
    bool counted_indirect_used { false };
    bool available { false };
  };

  OXGN_RNDR_API explicit ConventionalShadowRasterPass(
    std::shared_ptr<Config> config);
  OXGN_RNDR_API ~ConventionalShadowRasterPass() override;

  OXYGEN_DEFAULT_COPYABLE(ConventionalShadowRasterPass)
  OXYGEN_DEFAULT_MOVABLE(ConventionalShadowRasterPass)

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetCurrentOutput(ViewId view_id) const
    -> Output;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetIndirectPartitionsForInspection(
    ViewId view_id) const -> std::span<const IndirectPartitionInspection>;

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto UsesFramebufferDepthAttachment() const -> bool override;
  auto PublishesCanonicalDepthOutput() const -> bool override;
  auto BuildRasterizerStateDesc(graphics::CullMode cull_mode) const
    -> graphics::RasterizerStateDesc override;

private:
  auto SyncConfiguredDepthTextureFromShadowManager() -> void;
  auto CacheDeferredReclaimer() -> void;
  auto ReleaseShadowViewConstantsBuffer() noexcept -> void;
  auto ValidateRasterPlan(std::span<const renderer::RasterShadowJob> jobs) const
    -> void;
  auto EnsureShadowViewConstantsCapacity(std::uint32_t required_jobs) -> void;
  auto UploadJobViewConstants(std::span<const renderer::RasterShadowJob> jobs)
    -> void;
  auto BindJobViewConstants(
    graphics::CommandRecorder& recorder, std::uint32_t job_index) const -> void;
  auto ResetCurrentOutputState() -> void;
  auto PrepareJobDepthStencilView(graphics::Texture& depth_texture,
    const renderer::RasterShadowJob& job) const -> graphics::NativeView;

  struct ViewState {
    std::vector<IndirectPartitionInspection> inspection_partitions {};
    std::uint32_t job_count { 0U };
    bool counted_indirect_used { false };
    bool has_current_output { false };
  };

  std::shared_ptr<graphics::Buffer> shadow_view_constants_buffer_;
  void* shadow_view_constants_mapped_ptr_ { nullptr };
  std::uint32_t shadow_view_constants_capacity_ { 0U };
  observer_ptr<graphics::detail::DeferredReclaimer>
    shadow_view_constants_reclaimer_ { nullptr };
  std::unordered_map<ViewId, ViewState> view_states_ {};
};

} // namespace oxygen::engine

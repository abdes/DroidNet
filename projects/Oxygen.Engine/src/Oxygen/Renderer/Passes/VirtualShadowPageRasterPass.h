//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

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
  auto EnsureShadowViewConstantsCapacity(std::uint32_t required_jobs) -> void;
  auto UploadJobViewConstants(
    std::span<const renderer::VirtualShadowRasterJob> jobs) -> void;
  auto BindJobViewConstants(
    graphics::CommandRecorder& recorder, std::uint32_t job_index) const -> void;
  auto SetJobViewportAndScissors(graphics::CommandRecorder& recorder,
    const renderer::VirtualShadowRenderPlan& render_plan,
    const renderer::VirtualShadowRasterJob& job) const -> void;

  mutable graphics::NativeView full_atlas_dsv_ {};
  std::shared_ptr<graphics::Buffer> shadow_view_constants_buffer_;
  void* shadow_view_constants_mapped_ptr_ { nullptr };
  std::uint32_t shadow_view_constants_capacity_ { 0U };
  std::vector<ViewConstants::GpuData> job_view_constants_upload_;
};

} // namespace oxygen::engine

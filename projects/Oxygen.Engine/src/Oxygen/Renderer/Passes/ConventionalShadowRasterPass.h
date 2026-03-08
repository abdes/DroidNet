//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Types/RasterShadowRenderPlan.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

namespace oxygen::engine {

//! Conventional raster shadow-map rendering pass.
/*!
 Reuses the existing depth-only draw path and partition-aware depth PSOs, but
 consumes a generic raster shadow render plan instead of directional-only pass
 data.
*/
class ConventionalShadowRasterPass : public DepthPrePass {
public:
  using Config = DepthPrePassConfig;

  OXGN_RNDR_API explicit ConventionalShadowRasterPass(
    std::shared_ptr<Config> config);
  ~ConventionalShadowRasterPass() override;

  OXYGEN_DEFAULT_COPYABLE(ConventionalShadowRasterPass)
  OXYGEN_DEFAULT_MOVABLE(ConventionalShadowRasterPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto UsesFramebufferDepthAttachment() const -> bool override;
  auto BuildRasterizerStateDesc(graphics::CullMode cull_mode) const
    -> graphics::RasterizerStateDesc override;

private:
  auto EnsureShadowViewConstantsCapacity(std::uint32_t required_jobs) -> void;
  auto UploadJobViewConstants(std::span<const renderer::RasterShadowJob> jobs)
    -> void;
  auto BindJobViewConstants(
    graphics::CommandRecorder& recorder, std::uint32_t job_index) const -> void;
  auto PrepareJobDepthStencilView(graphics::Texture& depth_texture,
    const renderer::RasterShadowJob& job) const -> graphics::NativeView;

  std::shared_ptr<graphics::Buffer> shadow_view_constants_buffer_;
  void* shadow_view_constants_mapped_ptr_ { nullptr };
  std::uint32_t shadow_view_constants_capacity_ { 0U };
  std::vector<ViewConstants::GpuData> job_view_constants_upload_;
};

} // namespace oxygen::engine

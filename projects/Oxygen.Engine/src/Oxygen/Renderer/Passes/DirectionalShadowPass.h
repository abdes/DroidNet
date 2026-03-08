//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

namespace oxygen::engine {

//! Conventional directional shadow-map rendering pass.
/*!
 Reuses the existing depth-only draw path and partition-aware depth PSOs, but
 renders into the shadow system's directional `Texture2DArray` slices using the
 light-view `ViewConstants` snapshots published by `ShadowManager`.
*/
class DirectionalShadowPass : public DepthPrePass {
public:
  using Config = DepthPrePassConfig;

  OXGN_RNDR_API explicit DirectionalShadowPass(std::shared_ptr<Config> config);
  ~DirectionalShadowPass() override;

  OXYGEN_DEFAULT_COPYABLE(DirectionalShadowPass)
  OXYGEN_DEFAULT_MOVABLE(DirectionalShadowPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto UsesFramebufferDepthAttachment() const -> bool override;
  auto BuildRasterizerStateDesc(graphics::CullMode cull_mode) const
    -> graphics::RasterizerStateDesc override;

private:
  auto EnsureShadowViewConstantsCapacity(std::uint32_t required_snapshots)
    -> void;
  auto UploadShadowViewConstants(
    std::span<const ViewConstants::GpuData> snapshots) -> void;
  auto BindCascadeViewConstants(graphics::CommandRecorder& recorder,
    std::uint32_t cascade_index) const -> void;
  auto PrepareCascadeDepthStencilView(graphics::Texture& depth_texture,
    std::uint32_t array_slice) const -> graphics::NativeView;

  std::shared_ptr<graphics::Buffer> shadow_view_constants_buffer_;
  void* shadow_view_constants_mapped_ptr_ { nullptr };
  std::uint32_t shadow_view_constants_capacity_ { 0U };
};

} // namespace oxygen::engine

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
}

namespace oxygen::engine {

struct VirtualShadowCoarseMarkPassConfig {
  std::string debug_name { "VirtualShadowCoarseMarkPass" };
};

class VirtualShadowCoarseMarkPass : public ComputeRenderPass {
public:
  using Config = VirtualShadowCoarseMarkPassConfig;

  OXGN_RNDR_API VirtualShadowCoarseMarkPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VirtualShadowCoarseMarkPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowCoarseMarkPass)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowCoarseMarkPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  static constexpr std::uint32_t kDispatchGroupSize = 8U;
  static constexpr std::uint32_t kMaxSupportedPagesPerAxis = 64U;
  static constexpr std::uint32_t kMaxSupportedClipLevels = 12U;
  static constexpr std::uint32_t kMaxSupportedPageCount
    = kMaxSupportedPagesPerAxis * kMaxSupportedPagesPerAxis
    * kMaxSupportedClipLevels;
  static constexpr std::uint32_t kMaxRequestWordCount
    = (kMaxSupportedPageCount + 31U) / 32U;

  observer_ptr<Graphics> gfx_;
  std::shared_ptr<Config> config_;

  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  graphics::NativeView pass_constants_cbv_ {};
  ShaderVisibleIndex pass_constants_index_ { kInvalidShaderVisibleIndex };
  void* pass_constants_mapped_ptr_ { nullptr };

  ShaderVisibleIndex depth_texture_srv_ { kInvalidShaderVisibleIndex };
  const graphics::Texture* depth_texture_owner_ { nullptr };
  bool owns_depth_texture_srv_ { false };

  ViewId active_view_id_ {};
  std::uint32_t active_request_word_count_ { 0U };
  std::uint32_t active_pages_per_axis_ { 0U };
  std::uint32_t active_clip_level_count_ { 0U };
  std::uint32_t active_coarse_backbone_begin_ { 0U };
  std::array<std::int32_t, kMaxSupportedClipLevels>
    active_clip_grid_origin_x_ {};
  std::array<std::int32_t, kMaxSupportedClipLevels>
    active_clip_grid_origin_y_ {};
  bool active_dispatch_ { false };

  OXGN_RNDR_API auto EnsurePassConstantsBuffer() -> void;
  OXGN_RNDR_API auto EnsureDepthTextureSrv(const graphics::Texture& depth_tex)
    -> ShaderVisibleIndex;
};

} // namespace oxygen::engine

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
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

struct VirtualShadowAtlasDebugPassConfig {
  std::string debug_name { "VirtualShadowAtlasDebugPass" };
};

class VirtualShadowAtlasDebugPass final : public ComputeRenderPass {
public:
  using Config = VirtualShadowAtlasDebugPassConfig;

  OXGN_RNDR_API VirtualShadowAtlasDebugPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VirtualShadowAtlasDebugPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowAtlasDebugPass)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowAtlasDebugPass)

  [[nodiscard]] OXGN_RNDR_API auto GetOutputTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&;

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  static constexpr std::uint32_t kDispatchGroupSize = 8U;

  auto EnsurePassConstantsBuffer() -> void;
  auto EnsureSourceTextureSrv(const graphics::Texture& source_texture)
    -> bindless::ShaderVisibleIndex;
  auto EnsureOutputTexture(std::uint32_t width, std::uint32_t height) -> void;
  auto EnsureOutputTextureUav() -> bindless::ShaderVisibleIndex;
  static auto ResolveSourceSrvFormat(Format format) -> Format;

  observer_ptr<Graphics> gfx_;
  std::shared_ptr<Config> config_;

  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  graphics::DescriptorHandle pass_constants_cbv_handle_ {};
  graphics::NativeView pass_constants_cbv_view_ {};
  bindless::ShaderVisibleIndex pass_constants_index_ {
    kInvalidShaderVisibleIndex
  };
  void* pass_constants_mapped_ptr_ { nullptr };

  const graphics::Texture* source_texture_owner_ { nullptr };
  graphics::DescriptorHandle source_texture_srv_handle_ {};
  graphics::NativeView source_texture_srv_view_ {};
  bindless::ShaderVisibleIndex source_texture_srv_index_ {
    kInvalidShaderVisibleIndex
  };

  std::shared_ptr<graphics::Texture> output_texture_;
  graphics::DescriptorHandle output_texture_uav_handle_ {};
  graphics::NativeView output_texture_uav_view_ {};
  bindless::ShaderVisibleIndex output_texture_uav_index_ {
    kInvalidShaderVisibleIndex
  };

  bool active_dispatch_ { false };
  std::uint32_t active_width_ { 0U };
  std::uint32_t active_height_ { 0U };
};

} // namespace oxygen::engine

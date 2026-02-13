//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct RenderContext;

//! Configuration for the ground grid rendering pass.
struct GroundGridPassConfig {
  //! Optional explicit color texture to render into.
  std::shared_ptr<const graphics::Texture> color_texture = nullptr;

  bool enabled { true };
  float plane_size { 1000.0F };
  float spacing { 1.0F };
  uint32_t major_every { 10U };
  float line_thickness { 0.02F };
  float major_thickness { 0.04F };
  float axis_thickness { 0.06F };
  float fade_start { 0.0F };
  float fade_end { 0.0F };
  float fade_power { 2.0F };
  float thickness_max_scale { 64.0F };
  float depth_bias { 1e-4F };
  float horizon_boost { 0.35F };
  Vec2 origin { 0.0F, 0.0F };

  graphics::Color minor_color { 0.30F, 0.30F, 0.30F, 1.0F };
  graphics::Color major_color { 0.50F, 0.50F, 0.50F, 1.0F };
  graphics::Color axis_color_x { 0.90F, 0.20F, 0.20F, 1.0F };
  graphics::Color axis_color_y { 0.20F, 0.90F, 0.20F, 1.0F };
  graphics::Color origin_color { 1.0F, 1.0F, 1.0F, 1.0F };

  //! Debug name for diagnostics.
  std::string debug_name { "GroundGridPass" };
};

//! Ground grid rendering pass: draws an infinite grid on the Z=0 plane.
class GroundGridPass final : public GraphicsRenderPass {
public:
  using Config = GroundGridPassConfig;

  OXGN_RNDR_API explicit GroundGridPass(
    std::shared_ptr<GroundGridPassConfig> config);
  OXGN_RNDR_API ~GroundGridPass() override;

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  [[nodiscard]] auto GetColorTexture() const -> const graphics::Texture&;
  [[nodiscard]] auto GetDepthTexture() const -> const graphics::Texture*;
  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;

  auto SetupRenderTargets(graphics::CommandRecorder& recorder) const -> void;
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  auto EnsurePassConstantsBuffer() -> void;
  auto UpdatePassConstants() -> void;
  auto ReleasePassConstantsBuffer() -> void;

  std::shared_ptr<GroundGridPassConfig> config_;
  std::shared_ptr<graphics::Buffer> pass_constants_buffer_ {};
  std::byte* pass_constants_mapped_ptr_ { nullptr };
  ShaderVisibleIndex pass_constants_index_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex depth_srv_index_ { kInvalidShaderVisibleIndex };
  const graphics::Texture* last_depth_texture_ { nullptr };
};

} // namespace oxygen::engine

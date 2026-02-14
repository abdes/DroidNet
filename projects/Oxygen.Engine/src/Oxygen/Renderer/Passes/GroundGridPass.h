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

namespace oxygen {
class Graphics;
namespace graphics {
  class Buffer;
  class CommandRecorder;
  class Framebuffer;
  class Texture;
}
} // namespace oxygen

namespace oxygen::engine {

struct RenderContext;

//! Configuration for the ground grid rendering pass.
struct GroundGridPassConfig {
  //! Optional explicit color texture to render into.
  std::shared_ptr<const graphics::Texture> color_texture;

  bool enabled { true };
  float spacing { kDefaultSpacing };
  uint32_t major_every { kDefaultMajorEvery };
  float line_thickness { kDefaultLineThickness };
  float major_thickness { kDefaultMajorThickness };
  float axis_thickness { kDefaultAxisThickness };
  float fade_start { kDefaultFadeStart };
  float fade_power { kDefaultFadePower };
  float horizon_boost { kDefaultHorizonBoost };
  Vec2 origin { 0.0F, 0.0F };

  // NOLINTBEGIN(*-magic-numbers)
  graphics::Color minor_color { 0.16F, 0.16F, 0.16F, 1.0F };
  graphics::Color major_color { 0.20F, 0.20F, 0.20F, 1.0F };
  graphics::Color axis_color_x { 0.7F, 0.23F, 0.23F, 1.0F };
  graphics::Color axis_color_y { 0.23F, 0.7F, 0.23F, 1.0F };
  graphics::Color origin_color { 1.0F, 1.0F, 1.0F, 1.0F };
  // NOLINTEND(*-magic-numbers)

  //! Controls whether the grid lags behind the camera for a fluid feel.
  bool smooth_motion { kDefaultSmoothMotion };
  //! Time in seconds to reach the target position (approximate).
  float smooth_time { kDefaultSmoothTime };

  //! Debug name for diagnostics.
  std::string debug_name { "GroundGridPass" };

  static constexpr auto kDefaultSpacing = 1.0F;
  static constexpr auto kDefaultMajorEvery = 10U;
  static constexpr auto kDefaultLineThickness = 0.02F;
  static constexpr auto kDefaultMajorThickness = 0.04F;
  static constexpr auto kDefaultAxisThickness = 0.06F;
  static constexpr auto kDefaultFadeStart = 0.0F;
  static constexpr auto kDefaultFadePower = 2.0F;
  static constexpr auto kDefaultHorizonBoost = 0.35F;
  static constexpr auto kDefaultSmoothMotion = true;
  static constexpr auto kDefaultSmoothTime = 1.0F;
};

//! Ground grid rendering pass: draws an infinite grid on the Z=0 plane.
class GroundGridPass final : public GraphicsRenderPass {
public:
  using Config = GroundGridPassConfig;

  OXGN_RNDR_API explicit GroundGridPass(
    std::shared_ptr<GroundGridPassConfig> config);
  OXGN_RNDR_API ~GroundGridPass() override;

  GroundGridPass(const GroundGridPass&) = delete;
  GroundGridPass(GroundGridPass&&) = delete;
  auto operator=(const GroundGridPass&) -> GroundGridPass& = delete;
  auto operator=(GroundGridPass&&) -> GroundGridPass& = delete;

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

  // Helper methods for UpdatePassConstants
  struct GroundGridPassConstants;
  auto ComputeInvViewProj() const -> glm::mat4;
  auto UpdateDepthTexture(
    Graphics& graphics, GroundGridPassConstants& constants) -> void;
  auto UpdateExposureIndex(GroundGridPassConstants& constants) const -> void;
  auto ComputeGridOffset(GroundGridPassConstants& constants) -> void;
  auto FillConstantBuffer(GroundGridPassConstants& constants) const -> void;

  std::shared_ptr<GroundGridPassConfig> config_;
  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  std::byte* pass_constants_mapped_ptr_ { nullptr };
  ShaderVisibleIndex pass_constants_index_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex depth_srv_index_ { kInvalidShaderVisibleIndex };
  const graphics::Texture* last_depth_texture_ { nullptr };

  // Smoothing state for "fluid" grid movement
  glm::dvec3 smooth_pos_ { 0.0 };
  glm::dvec3 smooth_vel_ { 0.0 };
  bool first_frame_ { true };
};

} // namespace oxygen::engine

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine {

// FIXME: DUPLICATED IN SEVERAL PLACES - CENTRALIZE
//! Standardized exposure modes for rendering.
enum class ExposureMode : std::uint32_t {
  kManual = 0u,
  kAuto = 1u,
  kManualCamera = 2u,
};

//! Returns a string representation of the exposure mode.
OXGN_RNDR_API auto to_string(ExposureMode mode) -> std::string;

//! Standardized tonemapper selection.
enum class ToneMapper : std::uint32_t {
  kAcesFitted = 0U,
  kReinhard = 1U,
  kNone = 2U,
  kFilmic = 3U,
};

//! Returns a string representation of the tonemapper.
OXGN_RNDR_API auto to_string(ToneMapper mapper) -> std::string;

//! Configuration for tone mapping and exposure.
struct ToneMapPassConfig {
  //! HDR source texture to tonemap.
  std::shared_ptr<graphics::Texture> source_texture;

  //! SDR output texture (if null, uses framebuffer color attachment).
  std::shared_ptr<graphics::Texture> output_texture;

  //! Exposure mode selection.
  ExposureMode exposure_mode { ExposureMode::kManual };

  //! Manual exposure multiplier (linear scale, 1.0 = no change).
  float manual_exposure { 1.0F };

  //! Gamma correction factor (default 2.2).
  float gamma { 2.2F };

  //! Tonemapping operator to apply.
  ToneMapper tone_mapper { ToneMapper::kNone };

  //! Whether this pass is enabled.
  bool enabled { true };

  //! Debug label for diagnostics.
  std::string debug_name { "ToneMapPass" };
};

//! Post-processing pass for exposure control and tonemapping.
/*!
 Converts HDR intermediate texture to SDR output using configurable exposure
 and tonemapping operators. This pass is designed for the OnCompositing phase.
*/
class ToneMapPass final : public GraphicsRenderPass {
public:
  using Config = ToneMapPassConfig;

  OXGN_RNDR_API explicit ToneMapPass(std::shared_ptr<ToneMapPassConfig> config);
  OXGN_RNDR_API ~ToneMapPass() override;

  OXYGEN_MAKE_NON_COPYABLE(ToneMapPass)
  OXYGEN_DEFAULT_MOVABLE(ToneMapPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  auto ReleasePassConstantsBuffer() -> void;

  auto SetupRenderTargets(graphics::CommandRecorder& recorder) const -> void;
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  [[nodiscard]] auto GetOutputTexture() const -> const graphics::Texture&;
  [[nodiscard]] auto GetSourceTexture() const -> const graphics::Texture&;

  auto EnsurePassConstantsBuffer() -> void;
  auto EnsureSourceTextureSrv(const graphics::Texture& texture)
    -> ShaderVisibleIndex;
  auto UpdatePassConstants(ShaderVisibleIndex source_texture_index) -> void;

  static constexpr uint32_t kPassConstantsStride = 256U;
  static constexpr size_t kPassConstantsSlots = 8U;

  std::shared_ptr<Config> config_;

  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  std::byte* pass_constants_mapped_ptr_ { nullptr };
  std::array<ShaderVisibleIndex, kPassConstantsSlots>
    pass_constants_indices_ {};
  size_t pass_constants_slot_ { 0U };

  std::unordered_map<const graphics::Texture*, ShaderVisibleIndex>
    source_texture_srvs_;
};

} // namespace oxygen::engine

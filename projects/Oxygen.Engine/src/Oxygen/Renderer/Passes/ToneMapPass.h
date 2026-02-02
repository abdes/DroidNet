//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Standardized exposure modes for rendering.
enum class ExposureMode : std::uint32_t {
  kManual = 0u,
  kAuto = 1u,
};

//! Returns a string representation of the exposure mode.
OXGN_RNDR_API auto to_string(ExposureMode mode) -> std::string;

//! Standardized tonemapper selection.
enum class ToneMapper : std::uint32_t {
  kAcesFitted = 0u,
  kReinhard = 1u,
  kNone = 2u,
  kFilmic = 3u, // Added Filmic as per UI requirements
};

//! Returns a string representation of the tonemapper.
OXGN_RNDR_API auto to_string(ToneMapper mapper) -> std::string;

//! Configuration for tone mapping and exposure.
struct ToneMapPassConfig {
  ExposureMode exposure_mode { ExposureMode::kManual };
  float manual_exposure { 1.0F };
  ToneMapper tone_mapper { ToneMapper::kAcesFitted };
  bool enabled { true };
  std::string debug_name { "ToneMapPass" };
};

//! Post-processing pass for exposure control and tonemapping.
class ToneMapPass final : public GraphicsRenderPass {
public:
  using Config = ToneMapPassConfig;

  OXGN_RNDR_API explicit ToneMapPass(std::shared_ptr<ToneMapPassConfig> config);
  ~ToneMapPass() override;

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  std::shared_ptr<Config> config_;
};

} // namespace oxygen::engine

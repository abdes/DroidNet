//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Texture;
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::engine {

//! Forward shading pass for transparent (blended) geometry.
/*! \brief Consumes DrawMetadata (SoA) and issues only records classified with
    the transparent pass flag (bit1) set.

  This initial implementation reuses the generic bindless mesh shader
  (Passes/Forward/ForwardMesh.hlsl) and relies on the per-record flags written
  by Renderer::FinalizeScenePrepSoA. Blending state currently mirrors the
  default ShaderPass pipeline (depth test on, depth write off); future work
  will introduce explicit blend state customization and ordering validation
  (back-to-front or OIT).

  @note Pass flag bits are presently hard-coded in multiple translation units
  (see TODO in Renderer.cpp). They will be replaced by a centralized strongly
  typed enum before expanding the taxonomy (additive, decals, transmission).
*/
class TransparentPass : public GraphicsRenderPass {
public:
  struct Config {
    std::shared_ptr<graphics::Texture> color_texture; //!< Target color RT
    std::shared_ptr<graphics::Texture> depth_texture; //!< Shared depth buffer
    std::string debug_name { "TransparentPass" };
    //! Rasterization fill mode for this pass (default: solid)
    graphics::FillMode fill_mode { graphics::FillMode::kSolid };
  };

  OXGN_RNDR_API explicit TransparentPass(std::shared_ptr<Config> config);
  ~TransparentPass() override = default;

  OXYGEN_MAKE_NON_COPYABLE(TransparentPass)
  OXYGEN_DEFAULT_MOVABLE(TransparentPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  auto GetColorTexture() const -> const graphics::Texture&;
  auto GetDepthTexture() const -> const graphics::Texture*; // nullable

  std::shared_ptr<Config> config_;

  std::optional<graphics::GraphicsPipelineDesc> pso_single_sided_ {};
  std::optional<graphics::GraphicsPipelineDesc> pso_double_sided_ {};
};

} // namespace oxygen::engine

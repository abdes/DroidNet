//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Vortex/Passes/RenderPass.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

class GraphicsRenderPass : public RenderPass {
public:
  ~GraphicsRenderPass() override = default;

  OXYGEN_MAKE_NON_COPYABLE(GraphicsRenderPass)
  OXYGEN_DEFAULT_MOVABLE(GraphicsRenderPass)

protected:
  OXGN_VRTX_API explicit GraphicsRenderPass(
    std::string_view name, bool require_view_constants = true);

  [[nodiscard]] auto LastBuiltPsoDesc() const -> const auto&
  {
    return last_built_pso_desc_;
  }

  [[nodiscard]] auto RequiresViewConstants() const noexcept -> bool
  {
    return require_view_constants_;
  }

  OXGN_VRTX_API auto OnPrepareResources(graphics::CommandRecorder& recorder)
    -> void override;
  OXGN_VRTX_API auto OnExecute(graphics::CommandRecorder& recorder)
    -> void override;

  virtual auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc = 0;
  virtual auto NeedRebuildPipelineState() const -> bool = 0;
  virtual OXGN_VRTX_API auto DoSetupPipeline(
    graphics::CommandRecorder& recorder) -> void;

  auto RebindCommonRootParameters(graphics::CommandRecorder& recorder) const
    -> void;

private:
  auto BindPassConstantsIndexConstant(graphics::CommandRecorder& recorder,
    ShaderVisibleIndex pass_constants_index) const -> void;
  auto BindViewConstantsBuffer(graphics::CommandRecorder& recorder) const
    -> void;
  auto BindIndicesBuffer(graphics::CommandRecorder& recorder) const -> void;

  std::optional<graphics::GraphicsPipelineDesc> last_built_pso_desc_;
  bool require_view_constants_ { true };
};

} // namespace oxygen::vortex

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

class ComputeRenderPass : public RenderPass {
public:
  OXGN_VRTX_API explicit ComputeRenderPass(std::string_view name);
  ~ComputeRenderPass() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ComputeRenderPass)
  OXYGEN_DEFAULT_MOVABLE(ComputeRenderPass)

protected:
  [[nodiscard]] auto LastBuiltPsoDesc() const -> const auto&
  {
    return last_built_pso_desc_;
  }

  OXGN_VRTX_API auto OnPrepareResources(graphics::CommandRecorder& recorder)
    -> void override;
  OXGN_VRTX_API auto OnExecute(graphics::CommandRecorder& recorder)
    -> void override;

  virtual auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc = 0;
  virtual auto NeedRebuildPipelineState() const -> bool = 0;

private:
  std::optional<graphics::ComputePipelineDesc> last_built_pso_desc_;
};

} // namespace oxygen::vortex

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
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class ComputePipelineDesc;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct ConventionalShadowReceiverAnalysisPassConfig {
  std::string debug_name { "ConventionalShadowReceiverAnalysisPass" };
};

class ConventionalShadowReceiverAnalysisPass final : public ComputeRenderPass {
public:
  using Config = ConventionalShadowReceiverAnalysisPassConfig;

  struct Output {
    std::shared_ptr<const graphics::Buffer> analysis_buffer {};
    ShaderVisibleIndex analysis_srv_index { kInvalidShaderVisibleIndex };
    std::uint32_t job_count { 0U };
    bool available { false };
  };

  OXGN_RNDR_API ConventionalShadowReceiverAnalysisPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~ConventionalShadowReceiverAnalysisPass() override;

  OXYGEN_MAKE_NON_COPYABLE(ConventionalShadowReceiverAnalysisPass)
  OXYGEN_DEFAULT_MOVABLE(ConventionalShadowReceiverAnalysisPass)

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetCurrentOutput(ViewId view_id) const
    -> Output;

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::engine

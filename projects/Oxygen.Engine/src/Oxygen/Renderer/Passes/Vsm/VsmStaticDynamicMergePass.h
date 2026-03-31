//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class ComputePipelineDesc;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

struct VsmStaticDynamicMergePassConfig {
  std::string debug_name { "VsmStaticDynamicMergePass" };
};

struct VsmStaticDynamicMergePassInput {
  renderer::vsm::VsmPageAllocationFrame frame {};
  renderer::vsm::VsmPhysicalPoolSnapshot physical_pool {};
  std::vector<std::uint32_t> merge_candidate_logical_pages {};
};

// Phase G merges the static cache slice back into the lighting-visible dynamic
// slice after rasterization. The merge direction is fixed: slice 1 -> slice 0.
// Static recache remains a separate path; this pass only consumes existing
// static-slice content for dirty pages.
class VsmStaticDynamicMergePass final : public ComputeRenderPass {
public:
  using Config = VsmStaticDynamicMergePassConfig;

  OXGN_RNDR_API VsmStaticDynamicMergePass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmStaticDynamicMergePass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmStaticDynamicMergePass)
  OXYGEN_DEFAULT_MOVABLE(VsmStaticDynamicMergePass)

  OXGN_RNDR_API auto SetInput(VsmStaticDynamicMergePassInput input) -> void;
  OXGN_RNDR_API auto ResetInput() noexcept -> void;

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

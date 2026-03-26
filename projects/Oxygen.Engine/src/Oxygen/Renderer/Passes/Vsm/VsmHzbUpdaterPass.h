//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>

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

struct VsmHzbUpdaterPassConfig {
  std::string debug_name { "VsmHzbUpdaterPass" };
};

struct VsmHzbUpdaterPassInput {
  renderer::vsm::VsmPageAllocationFrame frame {};
  renderer::vsm::VsmPhysicalPoolSnapshot physical_pool {};
  renderer::vsm::VsmHzbPoolSnapshot hzb_pool {};
  // When true, untouched HZB pages are preserved from the existing pool
  // contents instead of being reset to the canonical far-depth value.
  bool can_preserve_existing_hzb_contents { false };
  // When true, every allocated physical page is rebuilt even if the
  // current-frame dirty selectors are empty.
  bool force_rebuild_all_allocated_pages { false };
};

// Phase H rebuilds the shadow-space HZB from the merged dynamic shadow slice.
// The pass owns two explicit behaviors:
// - select only the physical pages whose derived HZB data must be refreshed
// - rebuild page-local HZB mips selectively, then rebuild top mips globally
//
// Current-frame HZB-data availability is published separately through the cache
// manager after successful execution; this pass owns the GPU rebuild work, not
// the cache-frame lifecycle.
class VsmHzbUpdaterPass final : public ComputeRenderPass {
public:
  using Config = VsmHzbUpdaterPassConfig;

  OXGN_RNDR_API VsmHzbUpdaterPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmHzbUpdaterPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmHzbUpdaterPass)
  OXYGEN_DEFAULT_MOVABLE(VsmHzbUpdaterPass)

  OXGN_RNDR_API auto SetInput(VsmHzbUpdaterPassInput input) -> void;
  OXGN_RNDR_API auto ResetInput() noexcept -> void;

protected:
  auto ValidateConfig() -> void override;
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::engine

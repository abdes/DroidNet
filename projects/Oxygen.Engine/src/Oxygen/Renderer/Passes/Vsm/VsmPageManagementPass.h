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
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

enum class VsmPageManagementFinalStage : std::uint8_t {
  kReuse = 0,
  kPackAvailablePages = 1,
  kAllocateNewPages = 2,
};

struct VsmPageManagementPassConfig {
  VsmPageManagementFinalStage final_stage {
    VsmPageManagementFinalStage::kAllocateNewPages
  };
  std::string debug_name { "VsmPageManagementPass" };
};

// Phase D current-frame page-management pass.
//
// The CPU planner stays authoritative for reuse/allocation decisions. This pass
// uploads those compact decisions and rebuilds the current-frame GPU products
// through the stage 6-8 architecture sequence:
// - reuse current-frame mappings from reusable physical pages
// - pack still-available physical pages into a contiguous stack
// - apply deterministic fresh allocations from that stack
//
// This pass intentionally stays on RenderPass rather than ComputeRenderPass.
// It orchestrates three compute stages with distinct PSOs in one execution
// scope, so the single-PSO compute-pass helper is not a clean fit here.
class VsmPageManagementPass : public RenderPass {
public:
  using Config = VsmPageManagementPassConfig;

  OXGN_RNDR_API VsmPageManagementPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmPageManagementPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmPageManagementPass)
  OXYGEN_DEFAULT_MOVABLE(VsmPageManagementPass)

  OXGN_RNDR_API auto SetFrameInput(renderer::vsm::VsmPageAllocationFrame frame)
    -> void;
  OXGN_RNDR_API auto ResetFrameInput() noexcept -> void;

  OXGN_RNDR_NDAPI auto GetAvailablePageCountBuffer() const noexcept
    -> std::shared_ptr<const graphics::Buffer>;

protected:
  auto ValidateConfig() -> void override;
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto OnPrepareResources(graphics::CommandRecorder& recorder) -> void override;
  auto OnExecute(graphics::CommandRecorder& recorder) -> void override;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::engine

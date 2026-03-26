//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
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

struct VsmInvalidationPassConfig {
  std::string debug_name { "VsmInvalidationPass" };
};

struct VsmInvalidationPassInput {
  std::vector<renderer::vsm::VsmPageRequestProjection>
    previous_projection_records {};
  std::vector<renderer::vsm::VsmShaderPageTableEntry>
    previous_page_table_entries {};
  std::vector<renderer::vsm::VsmPhysicalPageMeta>
    previous_physical_page_metadata {};
  std::vector<renderer::vsm::VsmInvalidationWorkItem>
    invalidation_work_items {};
};

// Standalone Phase J compute component.
//
// This pass owns the dedicated GPU invalidation stage described by the VSM
// architecture: it uploads a previous-frame page-table/projection view plus a
// prepared invalidation workload, then marks invalidation bits in previous-
// frame physical-page metadata only.
class VsmInvalidationPass final : public ComputeRenderPass {
public:
  using Config = VsmInvalidationPassConfig;

  OXGN_RNDR_API VsmInvalidationPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmInvalidationPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmInvalidationPass)
  OXYGEN_DEFAULT_MOVABLE(VsmInvalidationPass)

  OXGN_RNDR_API auto SetInput(VsmInvalidationPassInput input) -> void;
  OXGN_RNDR_API auto ResetInput() noexcept -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetCurrentOutputPhysicalMetadataBuffer() const noexcept
    -> std::shared_ptr<const graphics::Buffer>;

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

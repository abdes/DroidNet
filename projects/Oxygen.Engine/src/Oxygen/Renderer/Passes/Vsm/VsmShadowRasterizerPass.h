// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShadowRasterJobs.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class GraphicsPipelineDesc;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

struct VsmShadowRasterizerPassConfig {
  std::string debug_name { "VsmShadowRasterizerPass" };
};

struct VsmShadowRasterizerPassInput {
  renderer::vsm::VsmPageAllocationFrame frame {};
  renderer::vsm::VsmPhysicalPoolSnapshot physical_pool {};
  std::vector<renderer::vsm::VsmPageRequestProjection> projections {};
  std::optional<ViewConstants::GpuData> base_view_constants {};
};

class VsmShadowRasterizerPass : public DepthPrePass {
public:
  using Config = VsmShadowRasterizerPassConfig;

  struct IndirectPartitionInspection {
    PassMask pass_mask {};
    std::uint32_t draw_count { 0U };
    std::uint32_t max_commands_per_page { 0U };
    const graphics::Buffer* command_buffer { nullptr };
    const graphics::Buffer* count_buffer { nullptr };
  };

  OXGN_RNDR_API VsmShadowRasterizerPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmShadowRasterizerPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmShadowRasterizerPass)
  OXYGEN_DEFAULT_MOVABLE(VsmShadowRasterizerPass)

  OXGN_RNDR_API auto SetInput(VsmShadowRasterizerPassInput input) -> void;
  OXGN_RNDR_API auto ResetInput() noexcept -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPreparedPageCount() const noexcept
    -> std::size_t;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPreparedPages() const noexcept
    -> std::span<const renderer::vsm::VsmShadowRasterPageJob>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto
  GetIndirectPartitionsForInspection() const noexcept
    -> std::span<const IndirectPartitionInspection>;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetDepthTexture() const
    -> const graphics::Texture& override;

protected:
  auto ValidateConfig() -> void override;
  auto OnPrepareResources(graphics::CommandRecorder& recorder) -> void override;
  auto OnExecute(graphics::CommandRecorder& recorder) -> void override;
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  auto UsesFramebufferDepthAttachment() const -> bool override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::engine

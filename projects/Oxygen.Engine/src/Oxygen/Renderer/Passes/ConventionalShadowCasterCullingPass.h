//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/Types/PassMask.h>
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

struct ConventionalShadowCasterCullingPassConfig {
  std::string debug_name { "ConventionalShadowCasterCullingPass" };
};

class ConventionalShadowCasterCullingPass final : public ComputeRenderPass {
public:
  using Config = ConventionalShadowCasterCullingPassConfig;

  struct IndirectPartitionInspection {
    PassMask pass_mask {};
    std::uint32_t partition_index { 0U };
    std::uint32_t draw_record_count { 0U };
    std::uint32_t max_commands_per_job { 0U };
    const graphics::Buffer* command_buffer { nullptr };
    const graphics::Buffer* count_buffer { nullptr };
  };

  struct Output {
    std::uint32_t job_count { 0U };
    std::uint32_t partition_count { 0U };
    bool available { false };
  };

  OXGN_RNDR_API ConventionalShadowCasterCullingPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~ConventionalShadowCasterCullingPass() override;

  OXYGEN_MAKE_NON_COPYABLE(ConventionalShadowCasterCullingPass)
  OXYGEN_DEFAULT_MOVABLE(ConventionalShadowCasterCullingPass)

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetCurrentOutput(ViewId view_id) const
    -> Output;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetIndirectPartitionsForInspection(
    ViewId view_id) const -> std::span<const IndirectPartitionInspection>;

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

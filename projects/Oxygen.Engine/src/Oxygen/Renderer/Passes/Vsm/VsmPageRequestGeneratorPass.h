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
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

struct VsmPageRequestGeneratorPassConfig {
  std::uint32_t max_projection_count { 128U };
  std::uint32_t max_virtual_page_count { 0U };
  // Coarse pages are a VSM page-policy output, not a scene-HZB pre-pass.
  bool enable_coarse_pages { true };
  bool enable_light_grid_pruning { true };
  std::string debug_name { "VsmPageRequestGeneratorPass" };
};

// Standalone Phase C compute component.
//
// This pass is intentionally not wired into the render graph yet. It owns the
// shader-visible request/projection buffers and the compute dispatch contract
// so Phase K can integrate it later without reshaping the ABI again.
class VsmPageRequestGeneratorPass : public ComputeRenderPass {
public:
  using Config = VsmPageRequestGeneratorPassConfig;

  OXGN_RNDR_API VsmPageRequestGeneratorPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmPageRequestGeneratorPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmPageRequestGeneratorPass)
  OXYGEN_DEFAULT_MOVABLE(VsmPageRequestGeneratorPass)

  OXGN_RNDR_API auto SetFrameInputs(
    std::vector<renderer::vsm::VsmPageRequestProjection> projections,
    std::uint32_t virtual_page_count) -> void;

  OXGN_RNDR_API auto ResetFrameInputs() noexcept -> void;

  OXGN_RNDR_NDAPI auto GetProjectionCount() const noexcept -> std::uint32_t;
  OXGN_RNDR_NDAPI auto GetVirtualPageCount() const noexcept -> std::uint32_t;
  OXGN_RNDR_NDAPI auto GetProjectionSrvIndex() const noexcept
    -> ShaderVisibleIndex;
  OXGN_RNDR_NDAPI auto GetPageRequestFlagsSrvIndex() const noexcept
    -> ShaderVisibleIndex;
  OXGN_RNDR_NDAPI auto GetProjectionBuffer() const noexcept
    -> std::shared_ptr<const graphics::Buffer>;
  OXGN_RNDR_NDAPI auto GetPageRequestFlagsBuffer() const noexcept
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

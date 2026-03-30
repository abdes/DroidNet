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
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
class ComputePipelineDesc;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct VsmProjectionPassConfig {
  std::string debug_name { "VsmProjectionPass" };
};

struct VsmProjectionPassInput {
  renderer::vsm::VsmPageAllocationFrame frame {};
  renderer::vsm::VsmPhysicalPoolSnapshot physical_pool {};
  std::shared_ptr<const graphics::Texture> scene_depth_texture {};
};

// Standalone Phase I projection/composite component.
//
// This pass stays inside the low-level VSM module until Phase K wires it into
// the main renderer pipeline. It owns:
// - GPU upload of current-frame projection records published on the cache frame
// - per-view screen-space shadow mask allocation
// - directional and local-light projection/composite dispatches
class VsmProjectionPass final : public ComputeRenderPass {
public:
  using Config = VsmProjectionPassConfig;

  struct ViewOutput {
    std::shared_ptr<const graphics::Texture> directional_shadow_mask_texture {};
    ShaderVisibleIndex directional_shadow_mask_srv_index {
      kInvalidShaderVisibleIndex
    };
    std::shared_ptr<const graphics::Texture> shadow_mask_texture {};
    ShaderVisibleIndex shadow_mask_srv_index { kInvalidShaderVisibleIndex };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    bool available { false };
  };

  OXGN_RNDR_API VsmProjectionPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VsmProjectionPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VsmProjectionPass)
  OXYGEN_DEFAULT_MOVABLE(VsmProjectionPass)

  OXGN_RNDR_API auto SetInput(VsmProjectionPassInput input) -> void;
  OXGN_RNDR_API auto ResetInput() noexcept -> void;

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetCurrentOutput(ViewId view_id) const
    -> ViewOutput;

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

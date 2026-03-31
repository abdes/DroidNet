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
class CommandRecorder;
class Texture;
class ComputePipelineDesc;
} // namespace oxygen::graphics

namespace oxygen::engine {

struct RenderContext;

struct SceneDepthDerivatives {
  std::shared_ptr<const graphics::Texture> closest_texture;
  std::shared_ptr<const graphics::Texture> furthest_texture;
  ShaderVisibleIndex closest_srv_index { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex furthest_srv_index { kInvalidShaderVisibleIndex };
  std::uint32_t width { 0U };
  std::uint32_t height { 0U };
  std::uint32_t mip_count { 0U };
  bool available { false };
};

struct ScreenHzbBuildPassConfig {
  std::string debug_name { "ScreenHzbBuildPass" };
};

class ScreenHzbBuildPass final : public ComputeRenderPass {
public:
  using Config = ScreenHzbBuildPassConfig;
  using Output = SceneDepthDerivatives;

  OXGN_RNDR_API ScreenHzbBuildPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~ScreenHzbBuildPass() override;

  OXYGEN_MAKE_NON_COPYABLE(ScreenHzbBuildPass)
  OXYGEN_DEFAULT_MOVABLE(ScreenHzbBuildPass)

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetCurrentOutput(ViewId view_id) const
    -> Output;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPreviousFrameOutput(
    ViewId view_id) const -> Output;

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

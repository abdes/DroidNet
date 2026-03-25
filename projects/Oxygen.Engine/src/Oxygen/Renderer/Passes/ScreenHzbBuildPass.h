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

struct ScreenHzbBuildPassConfig {
  std::string debug_name { "ScreenHzbBuildPass" };
};

class ScreenHzbBuildPass final : public ComputeRenderPass {
public:
  using Config = ScreenHzbBuildPassConfig;

  struct ViewOutput {
    std::shared_ptr<const graphics::Texture> texture;
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t mip_count { 0U };
    bool available { false };
  };

  OXGN_RNDR_API ScreenHzbBuildPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~ScreenHzbBuildPass() override;

  OXYGEN_MAKE_NON_COPYABLE(ScreenHzbBuildPass)
  OXYGEN_DEFAULT_MOVABLE(ScreenHzbBuildPass)

  [[nodiscard]] OXGN_RNDR_NDAPI auto GetCurrentOutput(ViewId view_id) const
    -> ViewOutput;
  [[nodiscard]] OXGN_RNDR_NDAPI auto GetPreviousFrameOutput(
    ViewId view_id) const -> ViewOutput;

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

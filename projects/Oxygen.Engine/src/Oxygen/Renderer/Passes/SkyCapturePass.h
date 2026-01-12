//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>

namespace oxygen::engine {

//! WIP: captures the scene sky into a cubemap for sky lighting.
/*!
 This pass is intentionally not wired into the renderer yet.

 @todo Implement cubemap render target creation and publishing to
   EnvironmentStaticDataManager (SkyLightSource::kCapturedScene).
*/
class SkyCapturePass final : public GraphicsRenderPass {
public:
  explicit SkyCapturePass(std::string name);
  ~SkyCapturePass() override = default;

private:
  auto ValidateConfig() -> void override { }
  auto DoPrepareResources([[maybe_unused]] graphics::CommandRecorder& recorder)
    -> co::Co<> override
  {
    co_return;
  }

  auto DoExecute([[maybe_unused]] graphics::CommandRecorder& recorder)
    -> co::Co<> override;

  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

  bool logged_unimplemented_ { false };
};

} // namespace oxygen::engine

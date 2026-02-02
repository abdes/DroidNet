//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/ToneMapPass.h>

namespace oxygen::engine {

auto to_string(ExposureMode mode) -> std::string
{
  switch (mode) {
  case ExposureMode::kManual:
    return "manual";
  case ExposureMode::kAuto:
    return "auto";
  }
  return "unknown";
}

auto to_string(ToneMapper mapper) -> std::string
{
  switch (mapper) {
  case ToneMapper::kAcesFitted:
    return "aces";
  case ToneMapper::kReinhard:
    return "reinhard";
  case ToneMapper::kFilmic:
    return "filmic";
  case ToneMapper::kNone:
    return "none";
  }
  return "unknown";
}

ToneMapPass::ToneMapPass(std::shared_ptr<ToneMapPassConfig> config)
  : GraphicsRenderPass(config ? config->debug_name : "ToneMapPass")
  , config_(std::move(config))
{
}

ToneMapPass::~ToneMapPass() = default;

auto ToneMapPass::DoPrepareResources(graphics::CommandRecorder& /*recorder*/)
  -> co::Co<>
{
  co_return;
}

auto ToneMapPass::DoExecute(graphics::CommandRecorder& /*recorder*/) -> co::Co<>
{
  co_return;
}

auto ToneMapPass::ValidateConfig() -> void
{
  // Placeholder for validation logic
}

auto ToneMapPass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
{
  throw std::runtime_error(
    "ToneMapPass::CreatePipelineStateDesc() not implemented");
}

auto ToneMapPass::NeedRebuildPipelineState() const -> bool { return false; }

} // namespace oxygen::engine

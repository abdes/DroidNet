//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SkyCapturePass.h"

#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/PipelineState.h>

namespace oxygen::engine {

SkyCapturePass::SkyCapturePass(std::string name)
  : GraphicsRenderPass(std::move(name))
{
}

auto SkyCapturePass::DoExecute(
  [[maybe_unused]] graphics::CommandRecorder& recorder) -> co::Co<>
{
  if (!logged_unimplemented_) {
    LOG_F(WARNING, "SkyCapturePass: not implemented; skipping");
    logged_unimplemented_ = true;
  }
  co_return;
}

auto SkyCapturePass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
{
  throw std::runtime_error(
    "SkyCapturePass: CreatePipelineStateDesc not implemented");
}

auto SkyCapturePass::NeedRebuildPipelineState() const -> bool { return false; }

} // namespace oxygen::engine

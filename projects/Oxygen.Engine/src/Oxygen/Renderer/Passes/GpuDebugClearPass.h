//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>

#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::engine {

class GpuDebugClearPass final : public ComputeRenderPass {
public:
  OXGN_RNDR_API explicit GpuDebugClearPass(observer_ptr<Graphics> gfx);
  OXGN_RNDR_API ~GpuDebugClearPass() override = default;

  OXYGEN_MAKE_NON_COPYABLE(GpuDebugClearPass)
  OXYGEN_DEFAULT_MOVABLE(GpuDebugClearPass)

protected:
  //=== ComputeRenderPass Interface ===---------------------------------------//

  auto ValidateConfig() -> void override;
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;

  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;
};

} // namespace oxygen::engine

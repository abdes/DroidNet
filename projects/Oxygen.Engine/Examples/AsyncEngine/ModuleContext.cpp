//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ModuleContext.h"

#include <Oxygen/Base/Logging.h>

#include "Modules/RenderGraphModule.h"

namespace oxygen::examples::asyncsim {

auto ModuleContext::GetRenderGraphBuilder() noexcept -> RenderGraphBuilder*
{
  if (!render_graph_module_) {
    LOG_F(WARNING,
      "[ModuleContext] No render graph module available for builder access");
    return nullptr;
  }

  if (current_phase_ != FramePhase::FrameGraph) {
    LOG_F(WARNING,
      "[ModuleContext] Render graph builder only available during FrameGraph "
      "phase "
      "(current phase: {})",
      static_cast<int>(current_phase_));
    return nullptr;
  }

  return &render_graph_module_->GetRenderGraphBuilder();
}

} // namespace oxygen::examples::asyncsim

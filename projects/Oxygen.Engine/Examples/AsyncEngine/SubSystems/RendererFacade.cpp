//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "./RendererFacade.h"
#include "../FrameContext.h"

using oxygen::examples::asyncsim::RendererFacade;

void RendererFacade::BeginFrame(const FrameContext& ctx)
{
  ctx.render_graph_builder
    = std::make_unique<oxygen::examples::asyncsim::RenderGraphBuilder>();
}

void RendererFacade::EndFrame(const FrameContext& ctx)
{
  (void)ctx;
  // Placeholder for any per-frame cleanup logic
  // Currently no operations are performed here
}

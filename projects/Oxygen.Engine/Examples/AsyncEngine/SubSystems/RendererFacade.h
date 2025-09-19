//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "../Renderer/Graph/RenderGraphBuilder.h"
#include <Oxygen/Core/FrameContext.h>

namespace oxygen::engine::asyncsim {

class RendererFacade {
public:
  static void BeginFrame(const FrameContext& ctx);
  static void EndFrame(const FrameContext& ctx);
};

} // namespace oxygen::engine::asyncsim

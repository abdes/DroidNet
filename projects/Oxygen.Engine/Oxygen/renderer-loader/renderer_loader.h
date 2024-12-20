//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/platform/types.h"
#include "oxygen/renderer/renderer_module.h"
#include "oxygen/renderer/types.h"

namespace oxygen::graphics {

  void CreateRenderer(GraphicsBackendType backend, PlatformPtr platform, const RendererProperties& renderer_props);
  void DestroyRenderer();
  auto GetRenderer() -> RendererPtr;

}  // namespace oxygen::renderer

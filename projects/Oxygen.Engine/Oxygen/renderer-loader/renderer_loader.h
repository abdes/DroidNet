//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "oxygen/renderer/renderer.h"
#include "oxygen/renderer/renderer_module.h"
#include "oxygen/renderer-loader/api_export.h"

namespace oxygen::graphics {

  OXYGEN_RENDERER_LOADER_API auto CreateRenderer(
    GraphicsBackendType backend,
    PlatformPtr platform,
    const RendererProperties& renderer_props)
    -> std::shared_ptr<Renderer>;

}  // namespace oxygen::renderer

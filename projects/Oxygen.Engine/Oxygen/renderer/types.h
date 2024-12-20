//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "oxygen/base/resource_handle.h"

namespace oxygen {

  // The number of frame buffers we manage
  constexpr uint32_t kFrameBufferCount{ 3 };

  namespace resources {
    constexpr ResourceHandle::ResourceTypeT kWindow = 1;
    constexpr ResourceHandle::ResourceTypeT kSurface = 2;
  }  // namespace resources

  using WindowId = ResourceHandle;
  using SurfaceId = ResourceHandle;

  struct RendererProperties
  {
    // Debugging support
    bool enable_debug{ false };

    // Validation and validation fine-grained control
    bool enable_validation{ false };
  };
  class Renderer;
  using RendererPtr = std::weak_ptr<Renderer>;

}

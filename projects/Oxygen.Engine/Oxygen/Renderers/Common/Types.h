//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "oxygen/base/resource_handle.h"

namespace oxygen {

  /**
   * Constants and types for render special resources.
   *
   * These resources are not managed by the backend graphics API, but are
   * managed by the render and treated as resources with a handle.
   */
  namespace renderer::resources {
    constexpr ResourceHandle::ResourceTypeT kWindow = 1;
    constexpr ResourceHandle::ResourceTypeT kSurface = 2;

    using WindowId = ResourceHandle;
    using SurfaceId = ResourceHandle;
  }  // namespace resources


  /// The number of frame buffers we manage
  constexpr uint32_t kFrameBufferCount{ 3 };

  /**
   * Forward declarations of renderer types and associated smart pointers.
   * @{
   */

  class Renderer;
  class IMemoryBlock;
  struct MemoryBlockDesc;

  using RendererPtr = std::weak_ptr<Renderer>;
  using MemoryBlockPtr = std::shared_ptr<IMemoryBlock>;

  /**@}*/

}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "Oxygen/Base/ResourceHandle.h"

namespace oxygen {

  //! Constants and types for render special resources.
  /*!
   * These resources are not managed by the backend graphics API, but are
   * managed by the render and treated as resources with a handle.
   */
  namespace renderer::resources {
    constexpr ResourceHandle::ResourceTypeT kWindow = 1;
    constexpr ResourceHandle::ResourceTypeT kSurface = 2;

    using WindowId = ResourceHandle;
    using SurfaceId = ResourceHandle;
  }  // namespace resources


  //! The number of frame buffers we manage
  constexpr uint32_t kFrameBufferCount{ 3 };

  namespace renderer {

    //! Types of command queues.
    enum class CommandListType : int8_t
    {
      kGraphics = 0,      //<! Graphics command queue.
      kCompute = 1,       //<! Compute command queue.
      kCopy = 2,          //<! Copy command queue.

      kNone = -1          //<! Invalid command queue.
    };

    inline auto to_string(CommandListType const& ct) {
      switch (ct) {
      case CommandListType::kGraphics: return "Graphics";
      case CommandListType::kCompute: return "Compute";
      case CommandListType::kCopy: return "Copy";
      case CommandListType::kNone: return "Unknown";
      }
      return "Unknown";
    }
  }  // namespace renderer

  //! Forward declarations of renderer types and associated smart pointers.
  //! @{

  class Renderer;
  using RendererPtr = std::weak_ptr<Renderer>;

  namespace renderer {
    class Surface;
    class WindowSurface;
    class SynchronizationCounter;
    class IMemoryBlock;
    struct MemoryBlockDesc;
    class CommandList;
    class CommandQueue;

    using SurfacePtr = std::shared_ptr<Surface>;
    using WindowSurfacePtr = std::unique_ptr<WindowSurface>;
    using MemoryBlockPtr = std::shared_ptr<IMemoryBlock>;
    using CommandListPtr = std::unique_ptr<CommandList>;
  }

  //! @}
}

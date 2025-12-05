//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/View.h>

namespace oxygen::graphics {
  class Surface;
  class Framebuffer;
  class Texture;
  class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen {
  class Graphics;
} // namespace oxygen

namespace oxygen::interop::module {

  class ViewManager;

  class EditorCompositor {
  public:
    explicit EditorCompositor(std::shared_ptr<oxygen::Graphics> graphics,
      ViewManager& view_manager);
    ~EditorCompositor();

    OXYGEN_MAKE_NON_COPYABLE(EditorCompositor)
      OXYGEN_MAKE_NON_MOVABLE(EditorCompositor)

      // Ensure framebuffers exist for the surface's current backbuffer
      void EnsureFramebuffersForSurface(const graphics::Surface& surface);

    // Main compositing entry point - handles all view-to-surface compositing
    void OnCompositing();

    // Cleanup resources for a surface
    void CleanupSurface(const graphics::Surface& surface);

  private:
    // Blit a source texture to the surface's backbuffer
    void CompositeToSurface(graphics::CommandRecorder& recorder,
      const graphics::Surface& surface,
      const graphics::Texture& source_texture,
      const ViewPort& destination_region);

    // Per-surface, per-backbuffer framebuffers
    // Key: Surface pointer
    // Value: Vector of Framebuffers (one per swapchain image)
    std::unordered_map<const graphics::Surface*,
      std::vector<std::shared_ptr<graphics::Framebuffer>>>
      surface_framebuffers_;

    std::weak_ptr<oxygen::Graphics> graphics_;
    oxygen::observer_ptr<ViewManager> view_manager_;
  };

} // namespace oxygen::interop::module

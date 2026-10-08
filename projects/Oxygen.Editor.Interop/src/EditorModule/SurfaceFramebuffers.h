//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>

#include "EditorModule/SurfaceRegistry.h"

namespace oxygen {
  class Graphics;
  namespace graphics {
    class Framebuffer;
    class Surface;
  } // namespace graphics
} // namespace oxygen

namespace oxygen::interop::module {

  //! Colour-only framebuffers wrapping each registered surface's backbuffers,
  //! used as the composition targets of the editor views.
  /*!
   Entries are keyed by the surface key, never by surface address, so a surface
   created at a reused address can never receive another surface's cached
   backbuffers. The owner invalidates an entry when its surface resizes or is
   released.
  */
  class SurfaceFramebuffers {
  public:
    explicit SurfaceFramebuffers(std::weak_ptr<Graphics> graphics);
    ~SurfaceFramebuffers();

    OXYGEN_MAKE_NON_COPYABLE(SurfaceFramebuffers)
    OXYGEN_MAKE_NON_MOVABLE(SurfaceFramebuffers)

    //! Framebuffer for the surface's current backbuffer, created on first use.
    [[nodiscard]] auto GetCurrent(const SurfaceRegistry::GuidKey& key,
      const graphics::Surface& surface)
      -> std::shared_ptr<graphics::Framebuffer>;

    //! Drops the cached framebuffers of a resized or released surface.
    void Invalidate(const SurfaceRegistry::GuidKey& key);

  private:
    std::weak_ptr<Graphics> graphics_;
    std::unordered_map<SurfaceRegistry::GuidKey,
      std::vector<std::shared_ptr<graphics::Framebuffer>>,
      SurfaceRegistry::GuidHasher>
      framebuffers_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)

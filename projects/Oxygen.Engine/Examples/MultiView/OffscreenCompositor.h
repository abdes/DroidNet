//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Forward.h>

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
}

namespace oxygen::examples::multiview {

//! Compositor for blitting offscreen render targets to swapchain backbuffer.
/*!
 Handles CopyTexture operations to composite multiple views into final output.
*/
class OffscreenCompositor {
public:
  OffscreenCompositor() = default;
  ~OffscreenCompositor() = default;

  OffscreenCompositor(const OffscreenCompositor&) = delete;
  OffscreenCompositor& operator=(const OffscreenCompositor&) = delete;

  //! Composite source texture to full backbuffer.
  /*!
   Copies entire source texture to backbuffer, filling the screen.
   \param recorder CommandRecorder for GPU commands
   \param source_texture Source render target
   \param backbuffer Destination swapchain framebuffer
  */
  auto CompositeFullscreen(graphics::CommandRecorder& recorder,
    graphics::Texture& source_texture, graphics::Texture& backbuffer) -> void;

  //! Composite source texture to region of backbuffer.
  /*!
   Copies source texture to specified viewport region of backbuffer.
   Used for picture-in-picture rendering.
   \param recorder CommandRecorder for GPU commands
   \param source_texture Source render target
   \param backbuffer Destination swapchain framebuffer
   \param viewport Destination region (x, y, width, height)
  */
  static auto CompositeToRegion(graphics::CommandRecorder& recorder,
    graphics::Texture& source_texture, graphics::Texture& backbuffer,
    const ViewPort& viewport) -> void;
};

} // namespace oxygen::examples::multiview

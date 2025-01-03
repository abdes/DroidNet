//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Graphics/Common/Renderer.h"
#include "Oxygen/Graphics/Common/RendererModule.h"

namespace oxygen::graphics {

  /**
   * Create a new renderer instance.
   *
   * This function creates a new renderer instance based on the specified
   * graphics backend type. The renderer is initialized with the provided
   * platform and renderer properties.
   *
   * @param backend The graphics backend type to use for the renderer.
   * @param platform The platform to use for the renderer initialization.
   * @param renderer_props The properties to initialize the renderer with.
   *
   * @throws runtime_error if loading the renderer module fails, or if the
   * module does not have the proper API entry points, or if the renderer
   * creation/initialization fails.
   */
  void CreateRenderer(
    GraphicsBackendType backend,
    PlatformPtr platform,
    const RendererProperties& renderer_props);

  /**
   * Destroy the renderer instance.
   *
   * This function destroys the current renderer instance, if any. When this
   * method is called, the Renderer::Shutdown() method is called, and any future
   * attempts to obtain a shared pointer to the renderer from the weak pointer
   * returned by GetRenderer() will fail.
   *
   * @note When the renderer is not initialized, this function has no effect,
   * except from logging a warning.
   */
  void DestroyRenderer() noexcept;

  /**
   * Get the current renderer instance.
   *
   * This function returns a weak pointer to the current renderer instance, that
   * needs to be locked before use. If the renderer instance has been destroyed,
   * the lock will return a null pointer.
   *
   * @return A weak pointer to the current renderer instance, or a null pointer
   * if no renderer exists.
   *
   * @note This function will abort the program if the renderer instance is not
   * properly initialized.
   */
  auto GetRenderer() noexcept -> RendererPtr;

}  // namespace oxygen::renderer

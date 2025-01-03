//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Graphics/Common/GraphicsModule.h"
#include "Oxygen/Graphics/Common/Renderer.h"

namespace oxygen::graphics {

//! Load the specified graphics backend.
//! \param backend The graphics backend type to load.
/*!
  \note The loaded backend is not initialized. Its Initialize method must be
  called before it can be used.
*/
auto LoadBackend(BackendType backend) -> GraphicsPtr;

//! Unloads the currently loaded graphics backend. If the backend has not been
//! shutdown, it will be.
void UnloadBackend() noexcept;

//! Get the currently loaded graphics backend.
/*!
  This function returns a weak pointer to the current renderer instance, that
  needs to be locked before use. when the backend gets unloaded, the lock will
  return a null pointer.

  \note This function will abort the program if the renderer instance is not
  properly initialized.
*/
auto GetBackend() noexcept -> GraphicsPtr;

} // namespace oxygen::renderer

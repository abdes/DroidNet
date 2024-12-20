//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

namespace oxygen::renderer::direct3d12 {

  class WindowSurface;
  class Renderer;

  using RendererPtr = std::shared_ptr<Renderer>;

}  // namespace oxygen::renderer::direct3d12

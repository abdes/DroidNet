//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

namespace oxygen::renderer::d3d12 {

  /**
   * Forward declarations of renderer types and associated smart pointers.
   * @{
   */

  class WindowSurface;
  class Renderer;
  class IDeferredReleaseController;

  using DeferredReleaseControllerPtr = std::weak_ptr<IDeferredReleaseController>;
  /**@}*/

}  // namespace oxygen::renderer::d3d12

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

#include <Oxygen/Renderer/RenderItemsList.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::engine::extraction {

//! Collect renderable items from a Scene using CPU culling.
/*!
 Performs a pre-order traversal with a visibility filter, ensures transforms
 are updated, builds one RenderItem per mesh, calls
 UpdateComputedProperties(), and inserts items that pass frustum culling.

 @return Number of items inserted into the output list.
 */
OXGN_RNDR_NDAPI auto CollectRenderItems(oxygen::scene::Scene& scene,
  const View& view, RenderItemsList& out) -> std::size_t;

} // namespace oxygen::engine::extraction

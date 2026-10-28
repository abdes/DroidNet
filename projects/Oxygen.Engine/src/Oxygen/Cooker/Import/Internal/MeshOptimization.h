//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/Vertex.h>

namespace oxygen::content::import::internal {

//! One submesh's indexed triangles.
/*!
 `joint_indices` and `joint_weights` are empty for a static mesh, else hold one
 entry per vertex.
*/
struct IndexedSurface final {
  std::vector<data::Vertex> vertices;
  std::vector<glm::uvec4> joint_indices;
  std::vector<glm::vec4> joint_weights;
  std::vector<uint32_t> indices;
};

//! The per-corner streams a submesh's triangles are read from.
/*!
 `joint_indices` and `joint_weights` are empty for a static mesh, else as long
 as `vertices`.
*/
struct CornerStreams final {
  std::span<const data::Vertex> vertices;
  std::span<const glm::uvec4> joint_indices;
  std::span<const glm::vec4> joint_weights;
};

//! Welds the triangles whose corners `corners` lists into an indexed surface.
/*!
 Corners whose vertex, joints and weights are bitwise equal share one vertex,
 so hard edges and UV seams, whose corners differ, stay split. Triangles keep
 their order and winding.
*/
OXGN_COOK_NDAPI auto WeldCorners(const CornerStreams& streams,
  std::span<const uint32_t> corners) -> IndexedSurface;

//! Triangles over a surface cut into consecutive runs.
struct TriangleRuns final {
  //! The triangles, three indices each, in run order.
  std::span<const uint32_t> indices;
  //! How many triangles each run takes, in order; they sum to the triangles.
  std::span<const uint32_t> triangle_counts;
};

//! Cuts `runs` over `source` into surfaces of their own, one per run, each
//! holding only the vertices its triangles use.
/*!
 Each vertex is visited once per run that uses it, so cutting a large surface
 into many runs costs the triangles it holds, not runs times its vertices.
*/
OXGN_COOK_NDAPI auto SplitTriangles(const IndexedSurface& source,
  const TriangleRuns& runs) -> std::vector<IndexedSurface>;

//! The triangles `indices` lists over `source`, ordered for the GPU.
/*!
 Triangles are reordered for the post-transform vertex cache, then vertices
 for fetch locality; the result holds only the vertices its triangles use.
*/
OXGN_COOK_NDAPI auto OrderForGpu(const IndexedSurface& source,
  std::span<const uint32_t> indices) -> IndexedSurface;

} // namespace oxygen::content::import::internal

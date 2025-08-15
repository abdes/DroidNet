//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::scene {

//! Describes the currently selected LOD mesh for this renderable.
/*!
 Value-type view exposing the selected Mesh and its LOD index. Clients
 can use the mesh pointer to access submeshes, bounds, and buffers.

 @warning The pointer references the immutable Mesh owned by the attached
 GeometryAsset. It's valid as long as the GeometryAsset remains alive.

 ### When the LOD index is needed

 - Streaming/residency: prefetch/evict adjacent LODs; track budgets per LOD
 - Stable IDs/diffing: compose (node, lod, submesh) for caches and telemetry
 - Render policy/batching: switch variants/pipelines or skip shadows by LOD
 - Bounds/occlusion: use LOD-specific bounds; debug popping/hysteresis
 - Tools/editor/QA: force LOD, visualize overlays, record in captures
 - Physics/nav/gameplay: audit parity or gate effects based on LOD level
 - HLOD/profiling: tune hysteresis, budgets, and transition thresholds
*/
struct ActiveMesh {
  std::shared_ptr<const data::Mesh> mesh; //!< Selected LOD mesh
  std::size_t lod { 0 }; //!< LOD index within asset
};

} // namespace oxygen::scene

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Core/Types/AssetKey.h>

namespace oxygen::content::import::internal {

//! The triangle surface of a cooked geometry, the input of the physics cooks
//! that read geometry: soft bodies, convex hulls and triangle meshes.
struct CookedMeshSurface final {
  //! The positions of the geometry's first mesh, all finite.
  std::vector<std::array<float, 3>> positions;
  //! The triangles of every mesh view of that mesh, as indices into
  //! `positions`, in authored order and winding. Degenerate triangles (an edge
  //! or the area below a millionth of the mesh extent) are dropped.
  std::vector<std::array<uint32_t, 3>> triangles;
};

//! Reads the surface of the geometry `geometry_key` that `content` lists.
/*!
 The first level of detail is read: a standard mesh from its vertex and index
 buffers, a procedural mesh by generating it. The descriptor and buffers are
 bounds-checked as untrusted input.

 @return The surface, or why the geometry cannot provide one.
*/
[[nodiscard]] auto ReadCookedMeshSurface(
  const cooked::CookedContent& content, const data::AssetKey& geometry_key)
  -> std::expected<CookedMeshSurface, std::string>;

} // namespace oxygen::content::import::internal

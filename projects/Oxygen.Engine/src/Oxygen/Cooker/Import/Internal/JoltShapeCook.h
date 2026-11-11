//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/CookedMeshSurface.h>

namespace oxygen::content::import::internal {

//! Makes Jolt usable by the cook: registers its default allocator, once.
auto EnsureJoltAllocatorReady() -> void;

//! The Jolt binary state of the convex hull of the triangle vertices of
//! `surface`, built with the radius and tolerance of `params`.
/*!
 The payload is what `JPH::Shape::sRestoreFromBinaryState` restores. Points
 are in the shape's local space, which is Oxygen's.

 @return The payload, or the error Jolt reports.
*/
[[nodiscard]] auto CookJoltConvexHull(
  const ConvexHullParams& params, const CookedMeshSurface& surface)
  -> std::expected<std::vector<std::byte>, std::string>;

//! The Jolt binary state of a static mesh of the triangles of `surface`, with
//! their winding (counter-clockwise front faces in both Oxygen and Jolt).
[[nodiscard]] auto CookJoltTriangleMesh(const CookedMeshSurface& surface)
  -> std::expected<std::vector<std::byte>, std::string>;

//! The Jolt binary state of the height field `params` describes.
/*!
 Jolt grids lie in its XZ plane with heights along +Y. The runtime turns the
 restored shape from Jolt +Y to Oxygen +Z, the way it turns every axial
 shape; that turn takes Jolt +Z to Oxygen -Y, so the cook stores the rows from
 the last one and offsets them by the grid depth. Sample `(row, column)` then
 lands at Oxygen `(column * spacing[0], row * spacing[1], height)`.
*/
[[nodiscard]] auto CookJoltHeightField(const HeightFieldParams& params)
  -> std::expected<std::vector<std::byte>, std::string>;

} // namespace oxygen::content::import::internal

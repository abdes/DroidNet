//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/Procedural/SurfaceLayout.h>

//! Closed solids of revolution about +Z, shared by the shapes built from them.
namespace oxygen::data::procedural {

//! A solid's extent along +Z and its radius.
struct AxialExtent final {
  float bottom_z;
  float top_z;
  float radius;
};

//! The look of a solid's vertices.
struct SolidStyle final {
  uint32_t segments;
  Vec4 color;
};

//! Appends a closed cylinder: a side and two caps, each with its own vertices.
/*!
 The side follows the revolution layout (seam at `Back`, v = 0 at the top),
 the caps the top and bottom face layouts. Returns false when the sampled
 surface collapses in float32 (a rim or the height loses distinct points).
*/
[[nodiscard]] auto AppendCylinder(MeshBuffers& buffers,
  const AxialExtent& extent, const SolidStyle& style) -> bool;

//! Appends a closed cone with its base at `bottom_z` and apex at `top_z`.
/*!
 The side follows the revolution layout with one apex vertex per segment, at
 the segment's middle u, so every side triangle has a valid frame; the base
 follows the bottom face layout. Returns false when the sampled surface
 collapses in float32.
*/
[[nodiscard]] auto AppendCone(MeshBuffers& buffers, const AxialExtent& extent,
  const SolidStyle& style) -> bool;

//! One latitude of a smooth closed surface of revolution: a pole or a ring.
struct Latitude final {
  //! Distance from the axis; zero at a pole.
  float ring_radius;
  float z;
  //! Sine and cosine of the polar angle from +Z of the outward normal.
  float sin_theta;
  float cos_theta;
  //! The texture's v, from 0 at the +Z pole to 1 at the -Z pole.
  float v;
};

//! Appends a smooth closed surface from its latitudes, +Z pole first.
/*!
 The first and last latitudes are poles: each takes one vertex per segment,
 at the segment's middle u, so no triangle is degenerate and every pole
 triangle has a valid frame. The rings in between take `segments + 1`
 vertices, the last repeating the first at u = 1. Returns false when an
 emitted triangle does not face outward in float32 storage (a collapsed
 surface).
*/
[[nodiscard]] auto AppendLatitudeSurface(MeshBuffers& buffers,
  std::span<const Latitude> latitudes, uint32_t segments) -> bool;

} // namespace oxygen::data::procedural

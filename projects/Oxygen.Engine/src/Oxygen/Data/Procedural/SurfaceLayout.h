//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <glm/geometric.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/Vertex.h>

//! The UV layout and tangent frames of generated surfaces.
/*!
 Every procedural shape takes its directions from here, which states
 `space::texture` "UV layout of generated surfaces" (Oxygen/Core/Constants.h)
 with the `space::move` axes. A frame's tangent is +dP/dU and its bitangent
 +dP/dV, and every layout is unmirrored: dot(cross(N, T), B) < 0.
*/
namespace oxygen::data::procedural {

//! The vertex and index buffers a generator fills.
struct MeshBuffers final {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

//! A planar face's outward normal, tangent (+U) and bitangent (+V).
struct FaceFrame final {
  Vec3 normal;
  Vec3 tangent;
  Vec3 bitangent;
};

//! The front face (-Y): the face a viewer facing the asset's front sees.
inline constexpr FaceFrame kFrontFace {
  .normal = space::move::Forward,
  .tangent = space::move::Right,
  .bitangent = space::move::Down,
};

//! The top face (+Z): image top toward the back.
inline constexpr FaceFrame kTopFace {
  .normal = space::move::Up,
  .tangent = space::move::Right,
  .bitangent = space::move::Forward,
};

//! The bottom face (-Z): image top toward the front.
inline constexpr FaceFrame kBottomFace {
  .normal = space::move::Down,
  .tangent = space::move::Right,
  .bitangent = space::move::Back,
};

//! The six faces of a box: the vertical faces have T = cross(Up, N) and
//! B = Down, then the top and bottom faces.
inline constexpr std::array kBoxFaces {
  kFrontFace,
  FaceFrame {
    .normal = space::move::Right,
    .tangent = space::move::Back,
    .bitangent = space::move::Down,
  },
  FaceFrame {
    .normal = space::move::Back,
    .tangent = space::move::Left,
    .bitangent = space::move::Down,
  },
  FaceFrame {
    .normal = space::move::Left,
    .tangent = space::move::Forward,
    .bitangent = space::move::Down,
  },
  kTopFace,
  kBottomFace,
};

//! Where a surface of revolution about +Z faces at one of its meridians.
struct RingFrame final {
  //! The horizontal unit direction from the axis.
  Vec3 outward;
  //! +dP/dU: counter-clockwise seen from +Z, `space::move::Right` at the
  //! front.
  Vec3 tangent;
};

//! The frame at turn `u` around +Z: u = 0 and 1 at `Back` (the seam), 0.25
//! at `Left`, 0.5 at `Forward` and 0.75 at `Right`.
[[nodiscard]] inline auto RingAtTurn(const double u) -> RingFrame
{
  constexpr auto kTwoPi = 2.0 * std::numbers::pi_v<double>;
  const auto angle = kTwoPi * u;
  const auto side = glm::cross(space::move::Up, space::move::Back);
  const auto outward = (space::move::Back * static_cast<float>(std::cos(angle)))
    + (side * static_cast<float>(std::sin(angle)));
  return {
    .outward = outward,
    .tangent = glm::cross(space::move::Up, outward),
  };
}

//! The frame of meridian `index` of `count`, at u = index / count. Meridian
//! `count` repeats meridian 0 exactly, so a seam's two columns coincide.
[[nodiscard]] inline auto RingAt(const uint32_t index, const uint32_t count)
  -> RingFrame
{
  return RingAtTurn(
    static_cast<double>(index % count) / static_cast<double>(count));
}

//! The outward normal and bitangent on a meridian at polar angle `theta`
//! from +Z: V grows with theta, from the +Z end down to the -Z end.
struct MeridianFrame final {
  Vec3 normal;
  Vec3 bitangent;
};

//! The meridian frame for `ring` given the polar angle's sine and cosine.
[[nodiscard]] inline auto MeridianAt(const RingFrame& ring,
  const float sin_theta, const float cos_theta) -> MeridianFrame
{
  return {
    .normal = (ring.outward * sin_theta) + (space::move::Up * cos_theta),
    .bitangent = (ring.outward * cos_theta) - (space::move::Up * sin_theta),
  };
}

//! Grid coordinate `index` of `count` cells over [-0.5, 0.5]: exactly
//! antisymmetric, so -GridOffset(k, n) == GridOffset(n - k, n) bit for bit and
//! faces sharing an edge share its positions exactly.
[[nodiscard]] inline auto GridOffset(const uint32_t index, const uint32_t count)
  -> float
{
  const auto numerator
    = (2.0 * static_cast<double>(index)) - static_cast<double>(count);
  return static_cast<float>(numerator / (2.0 * static_cast<double>(count)));
}

//! Texture coordinate `index` of `count` cells over [0, 1].
[[nodiscard]] inline auto GridCoordinate(
  const uint32_t index, const uint32_t count) -> float
{
  return static_cast<float>(
    static_cast<double>(index) / static_cast<double>(count));
}

} // namespace oxygen::data::procedural

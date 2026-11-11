//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::content::import::internal {

//! A sphere centred at the origin.
struct SphereParams final {
  float radius = 0.0F;
};

//! A capsule, cylinder or cone centred at the origin, along local +Z.
/*!
 A capsule's half height excludes its hemispheres and may be zero (a sphere);
 a cylinder's and a cone's is half their full height and is positive. A cone's
 apex is at +Z.
*/
struct AxialParams final {
  float radius = 0.0F;
  float half_height = 0.0F;
};

//! A box centred at the origin.
struct BoxParams final {
  std::array<float, 3> half_extents {};
};

//! The half space below the plane `dot(normal, p) + distance = 0`.
struct PlaneParams final {
  std::array<float, 3> normal {};
  float distance = 0.0F;
};

//! A world boundary between two corners, each limit strictly ordered.
struct WorldBoundaryParams final {
  data::pak::physics::WorldBoundaryMode mode
    = data::pak::physics::WorldBoundaryMode::kInvalid;
  std::array<float, 3> limits_min {};
  std::array<float, 3> limits_max {};
};

//! The convex hull of the triangles of a cooked geometry.
struct ConvexHullParams final {
  //! The default `convex_radius`: Jolt's default convex radius, in meters.
  static constexpr float kDefaultConvexRadius = 0.05F;
  //! The default `hull_tolerance`: Jolt's default, in meters.
  static constexpr float kDefaultHullTolerance = 1.0e-3F;

  std::string geometry_ref;
  //! Rounds the hull's edges; the backend may lower it to fit the hull.
  float convex_radius = kDefaultConvexRadius;
  //! How far a point may stay outside the hull; larger values give fewer
  //! hull vertices.
  float hull_tolerance = kDefaultHullTolerance;
};

//! The triangles of a cooked geometry, as a static collision mesh.
struct TriangleMeshParams final {
  std::string geometry_ref;
};

//! A regular grid of heights in local space.
/*!
 Sample `heights[row * sample_count + column]` is at local
 `(column * spacing[0], row * spacing[1], height)`: columns run along +X, rows
 along +Y and heights along +Z. A hole (no collision) is `std::nullopt`.
*/
struct HeightFieldParams final {
  //! The default `block_size`: Jolt's default.
  static constexpr uint32_t kDefaultBlockSize = 2U;
  //! The default `bits_per_sample`: Jolt's default.
  static constexpr uint32_t kDefaultBitsPerSample = 8U;

  uint32_t sample_count = 0U;
  std::vector<std::optional<float>> heights;
  std::array<float, 2> spacing { 1.0F, 1.0F };
  //! Samples per side of a culling block, in [2, 8].
  uint32_t block_size = kDefaultBlockSize;
  //! Bits that store one sample, in [1, 8].
  uint32_t bits_per_sample = kDefaultBitsPerSample;
};

//! A compound shape: its parts are the children of its source.
struct CompoundParams final { };

using ShapeParams = std::variant<SphereParams, AxialParams, BoxParams,
  PlaneParams, WorldBoundaryParams, ConvexHullParams, TriangleMeshParams,
  HeightFieldParams, CompoundParams>;

//! One shape with its local transform: a whole shape, or a compound child.
struct ShapePart final {
  data::pak::physics::ShapeType type = data::pak::physics::ShapeType::kInvalid;
  std::array<float, 3> local_position { 0.0F, 0.0F, 0.0F };
  //! A unit quaternion `[x, y, z, w]`.
  std::array<float, 4> local_rotation { 0.0F, 0.0F, 0.0F, 1.0F };
  //! Positive; uniform unless the type takes a non-uniform scale.
  std::array<float, 3> local_scale { 1.0F, 1.0F, 1.0F };
  ShapeParams params;
  //! Where the part is authored: empty for the shape, `/children/<i>` for a
  //! child.
  std::string json_pointer;
};

//! A collision shape descriptor, every value checked for what it cooks to.
struct CollisionShapeSource final {
  std::string name;
  std::string material_ref;
  bool is_sensor = false;
  //! The one layer the shape is on, a single bit.
  uint64_t collision_own_layer = 1ULL;
  uint64_t collision_target_layers = ~0ULL;
  ShapePart shape;
  //! The parts of a compound shape; empty for any other type.
  std::vector<ShapePart> children;
};

//! A geometry reference of a shape and where it is authored.
struct ShapeGeometryReference final {
  std::string reference;
  std::string json_pointer;
};

//! The geometry references of `source`: its convex hulls and triangle meshes.
OXGN_COOK_NDAPI auto GeometryReferencesOf(const CollisionShapeSource& source)
  -> std::vector<ShapeGeometryReference>;

//! Validates a collision shape descriptor; no file or cooked-resource I/O.
/*!
 Parses `bytes` as a JSON object, checks it against the descriptor schema, then
 checks every value against what it cooks to and what the runtime builds from
 it: numbers a float holds, positive dimensions, unit rotations, a uniform
 scale where the shape needs one, ordered world boundary limits, a single own
 layer bit and a complete height field. Resolving the references is not part
 of it.

 @param bytes Descriptor document text.
 @param source_path Path the diagnostics name.
 @param diagnostics Receives an error for every refusal.
 @return The checked shape, or nothing when an error was added.
*/
OXGN_COOK_NDAPI auto PrepareCollisionShapeDescriptor(std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<CollisionShapeSource>;

} // namespace oxygen::content::import::internal

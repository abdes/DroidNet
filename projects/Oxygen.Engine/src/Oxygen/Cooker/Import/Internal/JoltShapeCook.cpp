//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Jolt/Jolt.h> // IWYU pragma: keep

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ios>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <Jolt/Core/Array.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/Reference.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Geometry/IndexedTriangle.h>
#include <Jolt/Math/Float3.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/Import/Internal/JoltShapeCook.h>
#include <Oxygen/Cooker/Import/Internal/Utils/CookedMeshSurface.h>

namespace oxygen::content::import::internal {

namespace {

  using Payload = std::expected<std::vector<std::byte>, std::string>;

  //! Builds the shape `settings` describe and saves its binary state.
  auto SaveShape(const JPH::ShapeSettings& settings) -> Payload
  {
    const auto created = settings.Create();
    if (created.HasError()) {
      return std::unexpected(std::string(created.GetError()));
    }
    auto stream = std::ostringstream(std::ios::binary);
    auto wrapped = JPH::StreamOutWrapper(stream);
    created.Get()->SaveBinaryState(wrapped);
    if (wrapped.IsFailed()) {
      return std::unexpected(std::string("the shape state cannot be written"));
    }
    const auto blob = stream.str();
    auto bytes = std::vector<std::byte>(blob.size());
    std::ranges::transform(blob, bytes.begin(),
      [](const char c) -> std::byte { return static_cast<std::byte>(c); });
    return bytes;
  }

} // namespace

auto EnsureJoltAllocatorReady() -> void
{
  static std::once_flag once {};
  std::call_once(once, [] -> void { JPH::RegisterDefaultAllocator(); });
}

auto CookJoltConvexHull(
  const ConvexHullParams& params, const CookedMeshSurface& surface) -> Payload
{
  EnsureJoltAllocatorReady();
  // The hull of the surface: a vertex no triangle uses is not on it.
  auto used = std::vector<bool>(surface.positions.size(), false);
  for (const auto& triangle : surface.triangles) {
    for (const auto index : triangle) {
      used.at(index) = true;
    }
  }
  auto points = JPH::Array<JPH::Vec3> {};
  for (size_t i = 0; i < surface.positions.size(); ++i) {
    if (used.at(i)) {
      const auto& p = surface.positions.at(i);
      points.emplace_back(p[0], p[1], p[2]);
    }
  }
  auto settings = JPH::ConvexHullShapeSettings(points, params.convex_radius);
  settings.mHullTolerance = params.hull_tolerance;
  return SaveShape(settings);
}

auto CookJoltTriangleMesh(const CookedMeshSurface& surface) -> Payload
{
  EnsureJoltAllocatorReady();
  auto vertices = JPH::VertexList {};
  vertices.reserve(surface.positions.size());
  for (const auto& p : surface.positions) {
    vertices.emplace_back(p[0], p[1], p[2]);
  }
  auto triangles = JPH::IndexedTriangleList {};
  triangles.reserve(surface.triangles.size());
  for (const auto& t : surface.triangles) {
    triangles.emplace_back(t[0], t[1], t[2]);
  }
  // The constructor drops duplicate triangles.
  const auto settings = JPH::MeshShapeSettings(vertices, triangles);
  return SaveShape(settings);
}

auto CookJoltHeightField(const HeightFieldParams& params) -> Payload
{
  EnsureJoltAllocatorReady();
  const auto count = params.sample_count;
  auto samples = JPH::Array<float>(static_cast<size_t>(count) * count);
  for (uint32_t row = 0; row < count; ++row) {
    const auto jolt_row = count - 1U - row;
    for (uint32_t column = 0; column < count; ++column) {
      const auto& height
        = params.heights.at((static_cast<size_t>(row) * count) + column);
      samples.at((static_cast<size_t>(jolt_row) * count) + column)
        = height.value_or(JPH::HeightFieldShapeConstants::cNoCollisionValue);
    }
  }
  const auto depth = static_cast<float>(count - 1U) * params.spacing[1];
  auto settings = JPH::HeightFieldShapeSettings(samples.data(),
    JPH::Vec3(0.0F, 0.0F, -depth),
    JPH::Vec3(params.spacing[0], 1.0F, params.spacing[1]), count);
  settings.mBlockSize = params.block_size;
  settings.mBitsPerSample = params.bits_per_sample;
  return SaveShape(settings);
}

} // namespace oxygen::content::import::internal

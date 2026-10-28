//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <meshoptimizer.h>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/Import/Internal/MeshOptimization.h>
#include <Oxygen/Data/Vertex.h>

namespace oxygen::content::import::internal {
namespace {

  // Welding compares whole vertices bitwise: a vertex is its floats, with no
  // padding whose bytes could differ between equal corners.
  // NOLINTNEXTLINE(*-magic-numbers)
  static_assert(sizeof(data::Vertex) == 18U * sizeof(float));

  //! `source` laid out by `remap` into `count` vertices; empty stays empty.
  template <typename T>
  [[nodiscard]] auto Remap(const std::span<const T> source,
    const std::span<const uint32_t> remap, const std::size_t count)
    -> std::vector<T>
  {
    if (source.empty()) {
      return {};
    }
    auto out = std::vector<T>(count);
    meshopt_remapVertexBuffer(
      out.data(), source.data(), source.size(), sizeof(T), remap.data());
    return out;
  }

  //! The surface `remap` makes of `source`'s streams, with `count` vertices.
  [[nodiscard]] auto RemapStreams(const CornerStreams& source,
    const std::span<const uint32_t> remap, const std::size_t count)
    -> IndexedSurface
  {
    return IndexedSurface {
      .vertices = Remap(source.vertices, remap, count),
      .joint_indices = Remap(source.joint_indices, remap, count),
      .joint_weights = Remap(source.joint_weights, remap, count),
      .indices = {},
    };
  }

} // namespace

auto WeldCorners(const CornerStreams& streams,
  const std::span<const uint32_t> corners) -> IndexedSurface
{
  const auto skinned = !streams.joint_indices.empty();
  auto vertices = std::vector<data::Vertex> {};
  auto joint_indices = std::vector<glm::uvec4> {};
  auto joint_weights = std::vector<glm::vec4> {};
  vertices.reserve(corners.size());
  if (skinned) {
    joint_indices.reserve(corners.size());
    joint_weights.reserve(corners.size());
  }
  for (const auto corner : corners) {
    vertices.push_back(base::CheckedAt(streams.vertices, corner));
    if (skinned) {
      joint_indices.push_back(base::CheckedAt(streams.joint_indices, corner));
      joint_weights.push_back(base::CheckedAt(streams.joint_weights, corner));
    }
  }

  auto meshopt_streams = std::vector<meshopt_Stream> {
    {
      .data = vertices.data(),
      .size = sizeof(data::Vertex),
      .stride = sizeof(data::Vertex),
    },
  };
  if (skinned) {
    meshopt_streams.push_back({
      .data = joint_indices.data(),
      .size = sizeof(glm::uvec4),
      .stride = sizeof(glm::uvec4),
    });
    meshopt_streams.push_back({
      .data = joint_weights.data(),
      .size = sizeof(glm::vec4),
      .stride = sizeof(glm::vec4),
    });
  }

  auto remap = std::vector<uint32_t>(corners.size());
  const auto unique
    = meshopt_generateVertexRemapMulti(remap.data(), nullptr, corners.size(),
      corners.size(), meshopt_streams.data(), meshopt_streams.size());

  auto surface = RemapStreams(
    CornerStreams {
      .vertices = vertices,
      .joint_indices = joint_indices,
      .joint_weights = joint_weights,
    },
    remap, unique);
  surface.indices.resize(corners.size());
  meshopt_remapIndexBuffer(
    surface.indices.data(), nullptr, corners.size(), remap.data());
  return surface;
}

auto SplitTriangles(const IndexedSurface& source, const TriangleRuns& runs)
  -> std::vector<IndexedSurface>
{
  constexpr auto kUnused = std::numeric_limits<uint32_t>::max();
  const auto skinned = !source.joint_indices.empty();
  auto local = std::vector<uint32_t>(source.vertices.size(), kUnused);
  auto parts = std::vector<IndexedSurface> {};
  parts.reserve(runs.triangle_counts.size());
  auto offset = std::size_t { 0 };
  for (const auto triangles : runs.triangle_counts) {
    const auto run
      = runs.indices.subspan(offset, std::size_t { triangles } * 3U);
    offset += run.size();
    auto& part = parts.emplace_back();
    part.indices.reserve(run.size());
    for (const auto index : run) {
      auto& slot = local.at(index);
      if (slot == kUnused) {
        slot = static_cast<uint32_t>(part.vertices.size());
        part.vertices.push_back(source.vertices.at(index));
        if (skinned) {
          part.joint_indices.push_back(source.joint_indices.at(index));
          part.joint_weights.push_back(source.joint_weights.at(index));
        }
      }
      part.indices.push_back(slot);
    }
    // Only the run's own vertices were numbered; the next run starts clean.
    for (const auto index : run) {
      local.at(index) = kUnused;
    }
  }
  return parts;
}

auto OrderForGpu(const IndexedSurface& source,
  const std::span<const uint32_t> indices) -> IndexedSurface
{
  const auto vertex_count = source.vertices.size();
  auto ordered = std::vector<uint32_t>(indices.size());
  meshopt_optimizeVertexCache(
    ordered.data(), indices.data(), indices.size(), vertex_count);

  auto remap = std::vector<uint32_t>(vertex_count);
  const auto unique = meshopt_optimizeVertexFetchRemap(
    remap.data(), ordered.data(), ordered.size(), vertex_count);

  auto surface = RemapStreams(
    CornerStreams {
      .vertices = source.vertices,
      .joint_indices = source.joint_indices,
      .joint_weights = source.joint_weights,
    },
    remap, unique);
  surface.indices.resize(ordered.size());
  meshopt_remapIndexBuffer(
    surface.indices.data(), ordered.data(), ordered.size(), remap.data());
  return surface;
}

} // namespace oxygen::content::import::internal

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

#include <glm/gtc/type_ptr.hpp>
#include <meshoptimizer.h>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/LodGeneration.h>
#include <Oxygen/Cooker/Import/Internal/MeshOptimization.h>
#include <Oxygen/Data/Vertex.h>

namespace oxygen::content::import::internal {
namespace {

  // Relative priorities of the attributes against each other and against
  // positions; normals move less than texture coordinates and colours before
  // a collapse costs as much.
  constexpr auto kNormalWeight = 0.5F;
  constexpr auto kTexcoordWeight = 1.0F;
  constexpr auto kColorWeight = 1.0F;

  // Open borders stay in place so a surface does not crack away from its
  // neighbours; errors are in the geometry's units, the attribute part clamped
  // to the positional scale.
  constexpr auto kSimplifyOptions
    = static_cast<unsigned int>(meshopt_SimplifyLockBorder)
    | static_cast<unsigned int>(meshopt_SimplifyErrorAbsolute)
    | static_cast<unsigned int>(meshopt_SimplifyErrorClamped);

  // The share of a level's reduction it must remove from the previous level to
  // be kept.
  constexpr auto kLeastReductionShare = 0.5;

  constexpr auto kPercent = 100.0;

  using SurfaceIndices = std::vector<std::vector<uint32_t>>;

  // The simplifier reads positions as the first three floats of each vertex.
  static_assert(offsetof(data::Vertex, position) == 0);

  //! The weight of each attribute float `attributes` packs, in packing order.
  [[nodiscard]] auto WeightsOf(const SimplifiedAttributes& attributes)
    -> std::vector<float>
  {
    auto weights = std::vector<float> {};
    if (attributes.normals) {
      weights.insert(weights.end(), 3, kNormalWeight);
    }
    if (attributes.texcoords) {
      weights.insert(weights.end(), 2, kTexcoordWeight);
    }
    if (attributes.colors) {
      weights.insert(weights.end(), 4, kColorWeight);
    }
    return weights;
  }

  //! `surface`'s attributes, the floats `attributes` selects for each vertex.
  [[nodiscard]] auto PackAttributes(const IndexedSurface& surface,
    const SimplifiedAttributes& attributes) -> std::vector<float>
  {
    auto packed = std::vector<float> {};
    for (const auto& vertex : surface.vertices) {
      if (attributes.normals) {
        packed.insert(
          packed.end(), { vertex.normal.x, vertex.normal.y, vertex.normal.z });
      }
      if (attributes.texcoords) {
        packed.insert(packed.end(), { vertex.texcoord.x, vertex.texcoord.y });
      }
      if (attributes.colors) {
        packed.insert(packed.end(),
          { vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a });
      }
    }
    return packed;
  }

  [[nodiscard]] auto TriangleTotal(const SurfaceIndices& surfaces)
    -> std::size_t
  {
    auto total = std::size_t { 0 };
    for (const auto& indices : surfaces) {
      total += indices.size() / 3U;
    }
    return total;
  }

  //! What simplifying one surface gives: its triangles and their error.
  struct SimplifiedSurface final {
    std::vector<uint32_t> indices;
    float error = 0.0F;
  };

  //! The surface's triangles simplified toward `target_triangles`.
  [[nodiscard]] auto Simplify(const IndexedSurface& surface,
    const std::span<const float> attributes,
    const std::span<const float> weights, const std::size_t target_triangles)
    -> SimplifiedSurface
  {
    if (surface.vertices.empty() || surface.indices.empty()) {
      return {};
    }
    auto result = SimplifiedSurface {
      .indices = std::vector<uint32_t>(surface.indices.size()),
      .error = 0.0F,
    };
    const auto count = meshopt_simplifyWithAttributes(result.indices.data(),
      surface.indices.data(), surface.indices.size(),
      glm::value_ptr(surface.vertices.front().position),
      surface.vertices.size(), sizeof(data::Vertex),
      weights.empty() ? nullptr : attributes.data(),
      weights.size() * sizeof(float),
      weights.empty() ? nullptr : weights.data(), weights.size(), nullptr,
      target_triangles * 3U, std::numeric_limits<float>::max(),
      kSimplifyOptions, &result.error);
    result.indices.resize(count);
    return result;
  }

} // namespace

auto GenerateLods(const std::span<const IndexedSurface> lod0,
  const LodGenerationOptions& options, const SimplifiedAttributes& attributes,
  const std::stop_token& stop_token) -> GeneratedLods
{
  const auto weights = WeightsOf(attributes);
  auto packed = std::vector<std::vector<float>> {};
  packed.reserve(lod0.size());
  auto previous = SurfaceIndices {};
  previous.reserve(lod0.size());
  for (const auto& surface : lod0) {
    packed.push_back(PackAttributes(surface, attributes));
    previous.push_back(surface.indices);
  }

  const auto reduction
    = static_cast<double>(options.reduction_percent) / kPercent;
  auto previous_total = TriangleTotal(previous);
  auto previous_error = 0.0F;
  auto scale = 1.0;
  auto result = GeneratedLods {};
  for (uint32_t level = 1; level <= options.levels; ++level) {
    if (stop_token.stop_requested()) {
      return result;
    }
    scale *= 1.0 - reduction;
    auto lod = GeneratedLod {};
    lod.surface_indices.reserve(lod0.size());
    for (const auto& [index, surface] : std::views::enumerate(lod0)) {
      const auto at = static_cast<std::size_t>(index);
      const auto triangles = surface.indices.size() / 3U;
      const auto target = (std::max)(std::size_t { 1 },
        static_cast<std::size_t>(
          std::floor(static_cast<double>(triangles) * scale)));
      // A surface never gains triangles over the previous level nor loses
      // them all: such a result keeps the previous level's triangles.
      auto simplified = Simplify(surface, packed.at(at), weights, target);
      if (simplified.indices.empty()
        || simplified.indices.size() > previous.at(at).size()) {
        lod.surface_indices.push_back(previous.at(at));
        continue;
      }
      lod.error = (std::max)(lod.error, simplified.error);
      lod.surface_indices.push_back(std::move(simplified.indices));
    }

    lod.triangle_count = TriangleTotal(lod.surface_indices);
    const auto allowed
      = static_cast<std::size_t>(std::floor(static_cast<double>(previous_total)
        * (1.0 - (kLeastReductionShare * reduction))));
    if (lod.triangle_count > allowed) {
      result.stopped = StoppedLevel {
        .level = level,
        .triangle_count = lod.triangle_count,
        .allowed_triangle_count = allowed,
      };
      return result;
    }
    lod.error = (std::max)(lod.error, previous_error);
    previous_error = lod.error;
    previous_total = lod.triangle_count;
    previous = lod.surface_indices;
    result.lods.push_back(std::move(lod));
  }
  return result;
}

} // namespace oxygen::content::import::internal

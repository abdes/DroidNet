//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LodGeneration.cpp

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stop_token>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/LodGeneration.h>
#include <Oxygen/Cooker/Import/Internal/MeshOptimization.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {

  using internal::GeneratedLods;
  using internal::IndexedSurface;
  using internal::SimplifiedAttributes;

  constexpr auto kSegments = 32U;
  constexpr auto kRings = 16U;

  [[nodiscard]] auto VertexAt(const glm::vec3& position) -> data::Vertex
  {
    return data::Vertex {
      .position = position,
      .normal = glm::normalize(position),
      .texcoord = { 0.0F, 0.0F },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    };
  }

  //! A closed unit sphere: rings of `kSegments` vertices between two poles.
  [[nodiscard]] auto Sphere() -> IndexedSurface
  {
    auto sphere = IndexedSurface {};
    sphere.vertices.push_back(VertexAt({ 0.0F, 0.0F, 1.0F }));
    for (uint32_t ring = 1U; ring < kRings; ++ring) {
      const auto polar = std::numbers::pi_v<float>
        * static_cast<float>(ring) / static_cast<float>(kRings);
      for (uint32_t segment = 0U; segment < kSegments; ++segment) {
        const auto azimuth = 2.0F * std::numbers::pi_v<float>
          * static_cast<float>(segment) / static_cast<float>(kSegments);
        sphere.vertices.push_back(VertexAt({
          std::sin(polar) * std::cos(azimuth),
          std::sin(polar) * std::sin(azimuth),
          std::cos(polar),
        }));
      }
    }
    sphere.vertices.push_back(VertexAt({ 0.0F, 0.0F, -1.0F }));
    const auto south = static_cast<uint32_t>(sphere.vertices.size() - 1U);
    const auto at
      = [](const uint32_t ring, const uint32_t segment) -> uint32_t {
      return 1U + ((ring - 1U) * kSegments) + (segment % kSegments);
    };
    for (uint32_t segment = 0U; segment < kSegments; ++segment) {
      sphere.indices.insert(
        sphere.indices.end(), { 0U, at(1U, segment), at(1U, segment + 1U) });
      sphere.indices.insert(sphere.indices.end(),
        { south, at(kRings - 1U, segment + 1U), at(kRings - 1U, segment) });
    }
    for (uint32_t ring = 1U; ring + 1U < kRings; ++ring) {
      for (uint32_t segment = 0U; segment < kSegments; ++segment) {
        sphere.indices.insert(sphere.indices.end(),
          {
            at(ring, segment),
            at(ring + 1U, segment),
            at(ring + 1U, segment + 1U),
            at(ring, segment),
            at(ring + 1U, segment + 1U),
            at(ring, segment + 1U),
          });
      }
    }
    return sphere;
  }

  //! One open triangle: every vertex is on its border, so nothing collapses.
  [[nodiscard]] auto Triangle() -> IndexedSurface
  {
    return IndexedSurface {
      .vertices = {
        VertexAt({ 0.0F, 0.0F, 1.0F }),
        VertexAt({ 1.0F, 0.0F, 1.0F }),
        VertexAt({ 0.0F, 1.0F, 1.0F }),
      },
      .joint_indices = {},
      .joint_weights = {},
      .indices = { 0, 1, 2 },
    };
  }

  [[nodiscard]] auto Options(const uint32_t levels, const float reduction)
    -> LodGenerationOptions
  {
    return LodGenerationOptions {
      .enabled = true,
      .levels = levels,
      .reduction_percent = reduction,
    };
  }

  [[nodiscard]] auto Generate(const std::vector<IndexedSurface>& surfaces,
    const LodGenerationOptions& options) -> GeneratedLods
  {
    return internal::GenerateLods(surfaces, options,
      SimplifiedAttributes {
        .normals = true,
        .texcoords = false,
        .colors = false,
      },
      std::stop_token {});
  }

  [[nodiscard]] auto TriangleCount(const std::vector<uint32_t>& indices)
    -> std::size_t
  {
    return indices.size() / 3U;
  }

  //! Each level of a sphere keeps at most three quarters of the previous
  //! level's triangles, indexes LOD 0's vertices, and reports an error that
  //! grows with the level.
  NOLINT_TEST(LodGenerationTest, EachLevelReducesThePreviousOne)
  {
    const auto surfaces = std::vector { Sphere() };

    const auto generated = Generate(surfaces, Options(3U, 50.0F));

    EXPECT_FALSE(generated.stopped.has_value());
    ASSERT_EQ(generated.lods.size(), 3U);
    auto previous = TriangleCount(surfaces.front().indices);
    auto previous_error = 0.0F;
    for (const auto& lod : generated.lods) {
      ASSERT_EQ(lod.surface_indices.size(), 1U);
      const auto& indices = lod.surface_indices.front();
      EXPECT_EQ(lod.triangle_count, TriangleCount(indices));
      EXPECT_LE(TriangleCount(indices) * 4U, previous * 3U);
      EXPECT_TRUE(
        std::ranges::all_of(indices, [&](const uint32_t index) -> bool {
          return index < surfaces.front().vertices.size();
        }));
      EXPECT_GE(lod.error, previous_error);
      previous = TriangleCount(indices);
      previous_error = lod.error;
    }
    EXPECT_GT(previous_error, 0.0F);
  }

  //! A surface that cannot be simplified next to one that can keeps its
  //! triangles on every level: no surface loses every triangle.
  NOLINT_TEST(LodGenerationTest, EverySurfaceKeepsTriangles)
  {
    const auto surfaces = std::vector { Sphere(), Triangle() };

    const auto generated = Generate(surfaces, Options(2U, 75.0F));

    ASSERT_FALSE(generated.lods.empty());
    for (const auto& lod : generated.lods) {
      ASSERT_EQ(lod.surface_indices.size(), surfaces.size());
      EXPECT_GT(TriangleCount(lod.surface_indices.at(0)), 0U);
      EXPECT_EQ(TriangleCount(lod.surface_indices.at(1)), 1U);
    }
  }

  //! A geometry whose first level cannot reach its reduction generates no LOD
  //! and says where the chain stopped: the level, the triangles it reached
  //! and the most it could have kept.
  NOLINT_TEST(LodGenerationTest, ALevelThatCannotReduceStopsTheChain)
  {
    const auto surfaces = std::vector { Triangle() };

    const auto generated = Generate(surfaces, Options(3U, 50.0F));

    EXPECT_TRUE(generated.lods.empty());
    ASSERT_HAS_VALUE(generated.stopped);
    const auto& stopped = generated.stopped.value();
    EXPECT_EQ(stopped.level, 1U);
    EXPECT_EQ(stopped.triangle_count, 1U);
    EXPECT_EQ(stopped.allowed_triangle_count, 0U);
  }

} // namespace
} // namespace oxygen::content::import::test

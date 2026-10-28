//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/MeshOptimization.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint4.hpp>

#include <Oxygen/Cooker/Import/Internal/MeshOptimization.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {

  using internal::CornerStreams;
  using internal::IndexedSurface;

  [[nodiscard]] auto At(const float x, const float y,
    const glm::vec3 normal = { 0.0F, 0.0F, 1.0F }) -> data::Vertex
  {
    return data::Vertex {
      .position = { x, y, 0.0F },
      .normal = normal,
      .texcoord = { x, y },
      .tangent = { 1.0F, 0.0F, 0.0F },
      .bitangent = { 0.0F, 1.0F, 0.0F },
      .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    };
  }

  //! The positions of each triangle of `surface`, in triangle order.
  [[nodiscard]] auto Triangles(const IndexedSurface& surface)
    -> std::vector<std::array<glm::vec3, 3>>
  {
    auto triangles = std::vector<std::array<glm::vec3, 3>> {};
    for (std::size_t i = 0; i + 2U < surface.indices.size(); i += 3U) {
      triangles.push_back({
        surface.vertices.at(surface.indices.at(i)).position,
        surface.vertices.at(surface.indices.at(i + 1U)).position,
        surface.vertices.at(surface.indices.at(i + 2U)).position,
      });
    }
    return triangles;
  }

  //! Two triangles of a quad, given as six corners, share their diagonal's two
  //! vertices once welded; order and winding stay.
  NOLINT_TEST(MeshOptimizationTest, WeldingSharesEqualCorners)
  {
    const auto corners = std::array {
      At(0, 0),
      At(1, 0),
      At(1, 1),
      At(0, 0),
      At(1, 1),
      At(0, 1),
    };
    const auto order = std::array<uint32_t, 6> { 0, 1, 2, 3, 4, 5 };

    const auto surface
      = internal::WeldCorners(CornerStreams { .vertices = corners }, order);

    EXPECT_EQ(surface.vertices.size(), 4U);
    ASSERT_EQ(surface.indices.size(), 6U);
    EXPECT_TRUE(surface.joint_indices.empty());
    const auto triangles = Triangles(surface);
    ASSERT_EQ(triangles.size(), 2U);
    EXPECT_EQ(triangles.at(0).at(1), corners.at(1).position);
    EXPECT_EQ(triangles.at(1).at(2), corners.at(5).position);
  }

  //! Corners at one position with different normals, a hard edge, stay two
  //! vertices.
  NOLINT_TEST(MeshOptimizationTest, WeldingKeepsHardEdgesSplit)
  {
    const auto up = glm::vec3 { 0.0F, 0.0F, 1.0F };
    const auto side = glm::vec3 { 1.0F, 0.0F, 0.0F };
    const auto corners = std::array {
      At(0, 0, up),
      At(1, 0, up),
      At(1, 1, up),
      At(1, 0, side),
      At(1, 1, side),
      At(2, 0, side),
    };
    const auto order = std::array<uint32_t, 6> { 0, 1, 2, 3, 4, 5 };

    const auto surface
      = internal::WeldCorners(CornerStreams { .vertices = corners }, order);

    EXPECT_EQ(surface.vertices.size(), 6U);
  }

  //! A skinned corner welds only with corners of the same joints and weights,
  //! which follow their vertex.
  NOLINT_TEST(MeshOptimizationTest, WeldingCarriesJointsAndWeights)
  {
    const auto corners = std::array { At(0, 0), At(1, 0), At(1, 1), At(1, 1) };
    const auto joints = std::array {
      glm::uvec4 { 1, 0, 0, 0 },
      glm::uvec4 { 2, 0, 0, 0 },
      glm::uvec4 { 3, 0, 0, 0 },
      glm::uvec4 { 4, 0, 0, 0 },
    };
    const auto weights = std::array {
      glm::vec4 { 1, 0, 0, 0 },
      glm::vec4 { 1, 0, 0, 0 },
      glm::vec4 { 1, 0, 0, 0 },
      glm::vec4 { 1, 0, 0, 0 },
    };
    const auto order = std::array<uint32_t, 6> { 0, 1, 2, 0, 1, 3 };

    const auto surface = internal::WeldCorners(
      CornerStreams {
        .vertices = corners,
        .joint_indices = joints,
        .joint_weights = weights,
      },
      order);

    ASSERT_EQ(surface.vertices.size(), 4U);
    ASSERT_EQ(surface.joint_indices.size(), 4U);
    ASSERT_EQ(surface.joint_weights.size(), 4U);
    EXPECT_EQ(surface.joint_indices.at(surface.indices.at(2)).x, 3U);
    EXPECT_EQ(surface.joint_indices.at(surface.indices.at(5)).x, 4U);
  }

  //! Cutting a surface into runs gives each run only the vertices its own
  //! triangles use, a vertex shared by two runs once in each.
  NOLINT_TEST(MeshOptimizationTest, SplitGivesEachRunItsOwnVertices)
  {
    const auto source = IndexedSurface {
      .vertices = { At(0, 0), At(1, 0), At(1, 1), At(0, 1), At(5, 5) },
      .joint_indices = {},
      .joint_weights = {},
      .indices = { 0, 1, 2, 0, 2, 3 },
    };
    const auto runs = std::array<uint32_t, 2> { 1, 1 };

    const auto parts = internal::SplitTriangles(source,
      internal::TriangleRuns {
        .indices = source.indices,
        .triangle_counts = runs,
      });

    ASSERT_EQ(parts.size(), 2U);
    for (const auto& part : parts) {
      EXPECT_EQ(part.vertices.size(), 3U);
      EXPECT_EQ(part.indices, (std::vector<uint32_t> { 0, 1, 2 }));
    }
    EXPECT_EQ(Triangles(parts.at(0)),
      Triangles(IndexedSurface {
        .vertices = source.vertices,
        .joint_indices = {},
        .joint_weights = {},
        .indices = { 0, 1, 2 },
      }));
    EXPECT_EQ(parts.at(1).vertices.at(2).position, glm::vec3(0, 1, 0));
  }

  //! Ordering a subset of a surface's triangles keeps those triangles, holds
  //! only the vertices they use and lays the vertices out in first-use order.
  NOLINT_TEST(MeshOptimizationTest, OrderForGpuKeepsTheTrianglesItIsGiven)
  {
    const auto source = IndexedSurface {
      .vertices = { At(0, 0), At(1, 0), At(1, 1), At(0, 1), At(5, 5) },
      .joint_indices = {},
      .joint_weights = {},
      .indices = { 0, 1, 2, 0, 2, 3 },
    };
    const auto subset = std::span(source.indices).subspan(3, 3);

    const auto ordered = internal::OrderForGpu(source, subset);

    EXPECT_EQ(ordered.vertices.size(), 3U);
    ASSERT_EQ(ordered.indices.size(), 3U);
    auto first_use = std::vector<uint32_t> {};
    for (const auto index : ordered.indices) {
      if (std::ranges::find(first_use, index) == first_use.end()) {
        first_use.push_back(index);
      }
    }
    EXPECT_EQ(first_use, (std::vector<uint32_t> { 0, 1, 2 }));
    auto positions = std::vector<glm::vec3> {};
    for (const auto& vertex : ordered.vertices) {
      positions.push_back(vertex.position);
    }
    for (const auto index : subset) {
      EXPECT_NE(
        std::ranges::find(positions, source.vertices.at(index).position),
        positions.end());
    }
  }

} // namespace
} // namespace oxygen::content::import::test

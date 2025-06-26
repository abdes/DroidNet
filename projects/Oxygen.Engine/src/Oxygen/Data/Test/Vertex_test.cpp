//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/MeshAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>
#include <unordered_set>

using oxygen::data::QuantizedVertexHash;

namespace {
// VertexBasicTest: epsilon-based equality, quantized hash consistency
class VertexBasicTest : public testing::Test { };

NOLINT_TEST_F(VertexBasicTest, AlmostEqual_EpsilonBasedEquality)
{
  // Arrange
  constexpr Vertex v1 {
    .position = { 1.0f, 2.0f, 3.0f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };
  constexpr Vertex v2 {
    .position = { 1.0f + 1e-6f, 2.0f - 1e-6f, 3.0f + 1e-6f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };

  // Act
  // Assert
  EXPECT_EQ(v1, v2);
}

NOLINT_TEST_F(VertexBasicTest, QuantizedHash_Consistency)
{
  // Arrange
  constexpr Vertex v1 {
    .position = { 1.0f, 2.0f, 3.0f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };
  constexpr Vertex v2 {
    .position = { 1.0f + 1e-6f, 2.0f - 1e-6f, 3.0f + 1e-6f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };
  QuantizedVertexHash hasher;

  // Act
  auto h1 = hasher(v1);
  auto h2 = hasher(v2);

  // Assert
  EXPECT_EQ(h1, h2) << "QuantizedVertexHash should produce same hash for "
                       "almost equal vertices.";
}

// VertexEdgeTest: NaN, Inf, zero vectors
class VertexEdgeTest : public testing::Test { };

NOLINT_TEST_F(VertexEdgeTest, HandlesNaN)
{
  // Arrange
  Vertex v_nan {
    .position = { NAN, 0.0f, 0.0f },
    .normal = { 0.0f, NAN, 0.0f },
    .texcoord = { 0.0f, 0.0f },
    .tangent = { 0.0f, 0.0f, NAN },
    .bitangent = {},
    .color = {},
  };
  Vertex v_zero {
    .position = { 0.0f, 0.0f, 0.0f },
    .normal = { 0.0f, 0.0f, 0.0f },
    .texcoord = { 0.0f, 0.0f },
    .tangent = { 0.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };

  // Act & Assert
  EXPECT_FALSE(StrictlyEqual(v_nan, v_zero))
    << "NaN should not compare equal to zero (bitwise).";
}

NOLINT_TEST_F(VertexEdgeTest, HandlesInf)
{
  // Arrange
  Vertex v_inf {
    .position = { INFINITY, 0.0f, 0.0f },
    .normal = { 0.0f, INFINITY, 0.0f },
    .texcoord = { 0.0f, 0.0f },
    .tangent = { 0.0f, 0.0f, INFINITY },
    .bitangent = {},
    .color = {},
  };
  Vertex v_zero {
    .position = { 0.0f, 0.0f, 0.0f },
    .normal = { 0.0f, 0.0f, 0.0f },
    .texcoord = { 0.0f, 0.0f },
    .tangent = { 0.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };

  // Act & Assert
  EXPECT_FALSE(StrictlyEqual(v_inf, v_zero))
    << "Inf should not compare equal to zero (bitwise).";
}

NOLINT_TEST_F(VertexEdgeTest, HandlesZeroVectors)
{
  // Arrange
  Vertex v1 {
    .position = { 0.0f, 0.0f, 0.0f },
    .normal = { 0.0f, 0.0f, 0.0f },
    .texcoord = { 0.0f, 0.0f },
    .tangent = { 0.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };
  Vertex v2 {
    .position = { 0.0f, 0.0f, 0.0f },
    .normal = { 0.0f, 0.0f, 0.0f },
    .texcoord = { 0.0f, 0.0f },
    .tangent = { 0.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };

  // Act & Assert
  EXPECT_TRUE(StrictlyEqual(v1, v2))
    << "Zero vectors should compare equal (bitwise).";
}

// VertexHashTest: Vertex in hash-based containers with custom hash/equality
class VertexHashTest : public testing::Test { };

NOLINT_TEST_F(VertexHashTest, HashSet_AlmostEqualKey)
{
  // Arrange
  std::unordered_set<Vertex> vertex_set;
  Vertex v1 {
    .position = { 1.0f, 2.0f, 3.0f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };
  Vertex v2 {
    .position = { 1.0f + 1e-6f, 2.0f - 1e-6f, 3.0f + 1e-6f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };

  // Act
  vertex_set.insert(v1);
  vertex_set.insert(v2);

  // Assert
  EXPECT_EQ(vertex_set.size(), 1u)
    << "Hash set should treat almost equal vertices as the same key.";
}

} // namespace

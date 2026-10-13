//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <cmath>
#include <unordered_set>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/Vertex.h>

using oxygen::data::QuantizedVertexHash;
using oxygen::data::Vertex;

namespace {
//! Fixture for basic vertex equality and quantized hash behavior tests.
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
  // (Comparison occurs in assertion.)

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

//! Fixture for edge-case vertex comparisons (NaN, Inf, zero vectors).
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

  // Act
  // (StrictlyEqual evaluated in assertion.)

  // Assert
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

  // Act
  // (StrictlyEqual evaluated in assertion.)

  // Assert
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

  // Act
  // (StrictlyEqual evaluated in assertion.)

  // Assert
  EXPECT_TRUE(StrictlyEqual(v1, v2))
    << "Zero vectors should compare equal (bitwise).";
}

//! Fixture for vertex hashing scenarios in unordered containers.
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

//! Verifies that vertices differing beyond epsilon quantize to different
//! hashes.
NOLINT_TEST_F(VertexHashTest, QuantizedHash_DivergentBeyondEpsilon)
{
  // Arrange
  QuantizedVertexHash hasher; // epsilon = 1e-5
  Vertex v1 {
    .position = { 1.00000f, 2.00000f, 3.00000f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };
  // Choose deltas > epsilon (1e-5) to cross a quantization boundary.
  Vertex v2 {
    .position = { 1.0f + 2e-5f, 2.0f - 3e-5f, 3.0f + 4e-5f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.5f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = {},
    .color = {},
  };

  // Act
  auto h1 = hasher(v1);
  auto h2 = hasher(v2);

  // Assert
  EXPECT_NE(h1, h2)
    << "Hashes should differ when components differ beyond epsilon.";
  EXPECT_FALSE(v1 == v2)
    << "Equality operator should also report inequality beyond epsilon.";
}

//! Verifies equality & hash stability exactly at the epsilon boundary.
NOLINT_TEST_F(VertexHashTest, WithinEpsilon_EqualSameHash)
{
  // Arrange (base vertex + variant differing by <= epsilon on each component)
  using oxygen::data::kVertexEpsilon;
  Vertex base {
    .position = { 10.0f, -2.5f, 0.125f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.25f, 0.75f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = { 0.0f, 0.0f, 1.0f },
    .color = { 1.0f, 0.5f, 0.25f, 1.0f },
  };
  // Apply a small delta (<< epsilon) so quantization & equality both match.
  constexpr float delta = 1e-6f; // one tenth of kVertexEpsilon
  Vertex within {
    .position = { 10.0f + delta, -2.5f - delta, 0.125f + delta },
    .normal = { 0.0f, 1.0f - delta, 0.0f + delta },
    .texcoord = { 0.25f + delta, 0.75f - delta },
    .tangent = { 1.0f - delta, 0.0f + delta, 0.0f },
    .bitangent = { 0.0f, 0.0f + delta, 1.0f - delta },
    .color = { 1.0f - delta, 0.5f + delta, 0.25f - delta, 1.0f },
  };
  QuantizedVertexHash hasher; // epsilon = kVertexEpsilon

  // Act
  auto h_base = hasher(base);
  auto h_within = hasher(within);

  // Assert
  EXPECT_EQ(base, within)
    << "Vertices exactly at epsilon should compare equal.";
  EXPECT_EQ(h_base, h_within)
    << "Quantized hash must remain stable for components within <= epsilon.";
}

//! Verifies inequality & hash divergence for a delta just beyond epsilon.
NOLINT_TEST_F(VertexHashTest, JustBeyondEpsilon_InequalDifferentHash)
{
  // Arrange
  using oxygen::data::kVertexEpsilon;
  const float e = kVertexEpsilon;
  Vertex base {
    .position = { 5.0f, 6.0f, 7.0f },
    .normal = { 0.0f, 0.0f, 1.0f },
    .texcoord = { 0.1f, 0.9f },
    .tangent = { 0.0f, 1.0f, 0.0f },
    .bitangent = { 1.0f, 0.0f, 0.0f },
    .color = { 0.2f, 0.4f, 0.6f, 0.8f },
  };
  // Use 1.05 * epsilon deltas to minimally exceed tolerance.
  const float d = e * 1.05f;
  Vertex beyond {
    .position = { 5.0f + d, 6.0f - d, 7.0f },
    .normal = { 0.0f, d, 1.0f },
    .texcoord = { 0.1f, 0.9f + d },
    .tangent = { 0.0f, 1.0f, d },
    .bitangent = { 1.0f - d, 0.0f, 0.0f },
    .color = { 0.2f, 0.4f + d, 0.6f, 0.8f },
  };
  QuantizedVertexHash hasher;

  // Act
  auto h_base = hasher(base);
  auto h_beyond = hasher(beyond);

  // Assert
  EXPECT_NE(base, beyond)
    << "Vertex equality must fail when any component exceeds epsilon.";
  EXPECT_NE(h_base, h_beyond)
    << "Hash must diverge when a component exceeds quantization cell.";
}

//! Verifies that perturbing individual attribute groups changes the hash.
NOLINT_TEST_F(VertexHashTest, FieldPerturbations_ChangeHash)
{
  // Arrange
  using oxygen::data::kVertexEpsilon;
  const float e = kVertexEpsilon;
  Vertex base {
    .position = { 1.0f, 2.0f, 3.0f },
    .normal = { 0.0f, 1.0f, 0.0f },
    .texcoord = { 0.5f, 0.25f },
    .tangent = { 1.0f, 0.0f, 0.0f },
    .bitangent = { 0.0f, 1.0f, 0.0f },
    .color = { 0.9f, 0.8f, 0.7f, 1.0f },
  };
  QuantizedVertexHash hasher;
  const auto h_base = hasher(base);

  // Act & Assert (each perturbation > epsilon must change hash & equality)
  {
    Vertex v = base;
    v.position.x += 2 * e;
    EXPECT_NE(hasher(v), h_base);
    EXPECT_FALSE(v == base) << "position.x";
  }
  {
    Vertex v = base;
    v.normal.y += 2 * e;
    EXPECT_NE(hasher(v), h_base);
    EXPECT_FALSE(v == base) << "normal.y";
  }
  {
    Vertex v = base;
    v.texcoord.x += 2 * e;
    EXPECT_NE(hasher(v), h_base);
    EXPECT_FALSE(v == base) << "texcoord.x";
  }
  {
    Vertex v = base;
    v.tangent.z += 2 * e;
    EXPECT_NE(hasher(v), h_base);
    EXPECT_FALSE(v == base) << "tangent.z";
  }
  {
    Vertex v = base;
    v.bitangent.x += 2 * e;
    EXPECT_NE(hasher(v), h_base);
    EXPECT_FALSE(v == base) << "bitangent.x";
  }
  {
    Vertex v = base;
    v.color.r -= 2 * e;
    EXPECT_NE(hasher(v), h_base);
    EXPECT_FALSE(v == base) << "color.r";
  }
}

} // namespace

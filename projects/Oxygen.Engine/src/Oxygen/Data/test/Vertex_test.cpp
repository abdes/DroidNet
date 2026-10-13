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
    .position = { 1.0F, 2.0F, 3.0F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.5F },
    .tangent = { 1.0F, 0.0F, 0.0F },
    .bitangent = {},
    .color = {},
  };
  constexpr Vertex v2 {
    .position = { 1.0F + 1e-6F, 2.0F - 1e-6F, 3.0F + 1e-6F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.5F },
    .tangent = { 1.0F, 0.0F, 0.0F },
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
    .position = { 1.0F, 2.0F, 3.0F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.5F },
    .tangent = { 1.0F, 0.0F, 0.0F },
    .bitangent = {},
    .color = {},
  };
  constexpr Vertex v2 {
    .position = { 1.0F + 1e-6F, 2.0F - 1e-6F, 3.0F + 1e-6F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.5F },
    .tangent = { 1.0F, 0.0F, 0.0F },
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
    .position = { NAN, 0.0F, 0.0F },
    .normal = { 0.0F, NAN, 0.0F },
    .texcoord = { 0.0F, 0.0F },
    .tangent = { 0.0F, 0.0F, NAN },
    .bitangent = {},
    .color = {},
  };
  Vertex v_zero {
    .position = { 0.0F, 0.0F, 0.0F },
    .normal = { 0.0F, 0.0F, 0.0F },
    .texcoord = { 0.0F, 0.0F },
    .tangent = { 0.0F, 0.0F, 0.0F },
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
    .position = { INFINITY, 0.0F, 0.0F },
    .normal = { 0.0F, INFINITY, 0.0F },
    .texcoord = { 0.0F, 0.0F },
    .tangent = { 0.0F, 0.0F, INFINITY },
    .bitangent = {},
    .color = {},
  };
  Vertex v_zero {
    .position = { 0.0F, 0.0F, 0.0F },
    .normal = { 0.0F, 0.0F, 0.0F },
    .texcoord = { 0.0F, 0.0F },
    .tangent = { 0.0F, 0.0F, 0.0F },
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
    .position = { 0.0F, 0.0F, 0.0F },
    .normal = { 0.0F, 0.0F, 0.0F },
    .texcoord = { 0.0F, 0.0F },
    .tangent = { 0.0F, 0.0F, 0.0F },
    .bitangent = {},
    .color = {},
  };
  Vertex v2 {
    .position = { 0.0F, 0.0F, 0.0F },
    .normal = { 0.0F, 0.0F, 0.0F },
    .texcoord = { 0.0F, 0.0F },
    .tangent = { 0.0F, 0.0F, 0.0F },
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
    .position = { 1.0F, 2.0F, 3.0F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.5F },
    .tangent = { 1.0F, 0.0F, 0.0F },
    .bitangent = {},
    .color = {},
  };
  Vertex v2 {
    .position = { 1.0F + 1e-6F, 2.0F - 1e-6F, 3.0F + 1e-6F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.5F },
    .tangent = { 1.0F, 0.0F, 0.0F },
    .bitangent = {},
    .color = {},
  };

  // Act
  vertex_set.insert(v1);
  vertex_set.insert(v2);

  // Assert
  EXPECT_EQ(vertex_set.size(), 1U)
    << "Hash set should treat almost equal vertices as the same key.";
}

//! Verifies that vertices differing beyond epsilon quantize to different
//! hashes.
NOLINT_TEST_F(VertexHashTest, QuantizedHash_DivergentBeyondEpsilon)
{
  // Arrange
  QuantizedVertexHash hasher; // epsilon = 1e-5
  Vertex v1 {
    .position = { 1.00000F, 2.00000F, 3.00000F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.5F },
    .tangent = { 1.0F, 0.0F, 0.0F },
    .bitangent = {},
    .color = {},
  };
  // Choose deltas > epsilon (1e-5) to cross a quantization boundary.
  Vertex v2 {
    .position = { 1.0F + 2e-5F, 2.0F - 3e-5F, 3.0F + 4e-5F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.5F },
    .tangent = { 1.0F, 0.0F, 0.0F },
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
    .position = { 10.0F, -2.5F, 0.125F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.25F, 0.75F },
    .tangent = { 1.0F, 0.0F, 0.0F },
    .bitangent = { 0.0F, 0.0F, 1.0F },
    .color = { 1.0F, 0.5F, 0.25F, 1.0F },
  };
  // Apply a small delta (<< epsilon) so quantization & equality both match.
  constexpr float delta = 1e-6F; // one tenth of kVertexEpsilon
  Vertex within {
    .position = { 10.0F + delta, -2.5F - delta, 0.125F + delta },
    .normal = { 0.0F, 1.0F - delta, 0.0F + delta },
    .texcoord = { 0.25F + delta, 0.75F - delta },
    .tangent = { 1.0F - delta, 0.0F + delta, 0.0F },
    .bitangent = { 0.0F, 0.0F + delta, 1.0F - delta },
    .color = { 1.0F - delta, 0.5F + delta, 0.25F - delta, 1.0F },
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
    .position = { 5.0F, 6.0F, 7.0F },
    .normal = { 0.0F, 0.0F, 1.0F },
    .texcoord = { 0.1F, 0.9F },
    .tangent = { 0.0F, 1.0F, 0.0F },
    .bitangent = { 1.0F, 0.0F, 0.0F },
    .color = { 0.2F, 0.4F, 0.6F, 0.8F },
  };
  // Use 1.05 * epsilon deltas to minimally exceed tolerance.
  const float d = e * 1.05F;
  Vertex beyond {
    .position = { 5.0F + d, 6.0F - d, 7.0F },
    .normal = { 0.0F, d, 1.0F },
    .texcoord = { 0.1F, 0.9F + d },
    .tangent = { 0.0F, 1.0F, d },
    .bitangent = { 1.0F - d, 0.0F, 0.0F },
    .color = { 0.2F, 0.4F + d, 0.6F, 0.8F },
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
    .position = { 1.0F, 2.0F, 3.0F },
    .normal = { 0.0F, 1.0F, 0.0F },
    .texcoord = { 0.5F, 0.25F },
    .tangent = { 1.0F, 0.0F, 0.0F },
    .bitangent = { 0.0F, 1.0F, 0.0F },
    .color = { 0.9F, 0.8F, 0.7F, 1.0F },
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

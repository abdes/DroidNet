//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::GenerateMesh;
using oxygen::data::GenerateMeshBuffers;
using oxygen::data::MakeCapsuleMeshAsset;

NOLINT_TEST(CapsuleTest, DefaultBoundsAreMetricCenteredAndZUp)
{
  const auto mesh = GenerateMesh("Capsule/Default", {});
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->BoundingBoxMin(), glm::vec3(-0.5F, -0.5F, -1.0F));
  EXPECT_EQ(mesh->BoundingBoxMax(), glm::vec3(0.5F, 0.5F, 1.0F));
  EXPECT_EQ(mesh->SubMeshes().size(), 1U);
}

NOLINT_TEST(CapsuleTest, SurfaceFramesAndTrianglesMatchTheAnalyticCapsule)
{
  // Include the sphere limit and minimum/odd tessellations as well as defaults.
  for (const auto segments : { 1U, 3U, 8U }) {
    for (const auto radial : { 3U, 9U, 32U }) {
      for (const auto height : { 1.0F, 2.0F, 4.0F }) {
        SCOPED_TRACE(testing::Message()
          << "segments=" << segments << ", radial=" << radial
          << ", height=" << height);
        const auto buffers
          = MakeCapsuleMeshAsset(segments, radial, height, 0.5F);
        ASSERT_TRUE(buffers.has_value());
        const auto& [vertices, indices] = *buffers;
        const auto half_cylinder = (height * 0.5F) - 0.5F;
        for (const auto& vertex : vertices) {
          const auto center = glm::vec3(
            0, 0, std::clamp(vertex.position.z, -half_cylinder, half_cylinder));
          const auto offset = vertex.position - center;
          EXPECT_NEAR(glm::length(offset), 0.5F, 1e-6F);
          EXPECT_LT(glm::length(offset * 2.0F - vertex.normal), 1e-6F);
          EXPECT_NEAR(glm::length(vertex.normal), 1.0F, 1e-6F);
          EXPECT_NEAR(glm::length(vertex.tangent), 1.0F, 1e-6F);
          EXPECT_NEAR(glm::length(vertex.bitangent), 1.0F, 1e-6F);
          EXPECT_NEAR(glm::dot(vertex.normal, vertex.tangent), 0.0F, 1e-6F);
          EXPECT_NEAR(glm::dot(vertex.normal, vertex.bitangent), 0.0F, 1e-6F);
          EXPECT_NEAR(glm::dot(vertex.tangent, vertex.bitangent), 0.0F, 1e-6F);
          EXPECT_GT(glm::dot(glm::cross(vertex.tangent, vertex.bitangent),
                      vertex.normal),
            0.999F);
          EXPECT_GE(vertex.texcoord.x, 0.0F);
          EXPECT_LE(vertex.texcoord.x, 1.0F);
          EXPECT_GE(vertex.texcoord.y, 0.0F);
          EXPECT_LE(vertex.texcoord.y, 1.0F);
        }
        ASSERT_EQ(indices.size() % 3U, 0U);
        for (auto triangle = size_t { 0 }; triangle < indices.size();
          triangle += 3U) {
          ASSERT_LT(indices.at(triangle), vertices.size());
          ASSERT_LT(indices.at(triangle + 1U), vertices.size());
          ASSERT_LT(indices.at(triangle + 2U), vertices.size());
          const auto& a = vertices.at(indices.at(triangle));
          const auto& b = vertices.at(indices.at(triangle + 1U));
          const auto& c = vertices.at(indices.at(triangle + 2U));
          const auto cross
            = glm::cross(b.position - a.position, c.position - a.position);
          // Also rejects duplicate equators or degenerate pole triangles.
          EXPECT_GT(glm::dot(cross, a.normal + b.normal + c.normal), 0.0F);
        }
        const auto stride = radial + 1U;
        for (auto row = size_t { 0 }; row < vertices.size(); row += stride) {
          const auto& first = vertices.at(row);
          const auto& last = vertices.at(row + radial);
          EXPECT_EQ(first.position, last.position);
          EXPECT_EQ(first.normal, last.normal);
          EXPECT_EQ(first.tangent, last.tangent);
          EXPECT_EQ(first.bitangent, last.bitangent);
          EXPECT_EQ(first.texcoord.x, 0.0F);
          EXPECT_EQ(last.texcoord.x, 1.0F);
          EXPECT_EQ(first.texcoord.y, last.texcoord.y);
        }
      }
    }
  }
}

NOLINT_TEST(CapsuleTest, RejectsInvalidAndNonFiniteParameters)
{
  EXPECT_FALSE(MakeCapsuleMeshAsset(0, 32).has_value());
  EXPECT_FALSE(MakeCapsuleMeshAsset(65, 32).has_value());
  EXPECT_FALSE(MakeCapsuleMeshAsset(8, 2).has_value());
  EXPECT_FALSE(MakeCapsuleMeshAsset(8, 257).has_value());
  EXPECT_FALSE(MakeCapsuleMeshAsset(8, 32, 0.9F, 0.5F).has_value());
  EXPECT_TRUE(MakeCapsuleMeshAsset(64, 256).has_value());
  for (const auto invalid :
    { 0.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity() }) {
    EXPECT_FALSE(MakeCapsuleMeshAsset(8, 32, invalid, 0.5F).has_value());
    EXPECT_FALSE(MakeCapsuleMeshAsset(8, 32, 2.0F, invalid).has_value());
  }
  EXPECT_FALSE(
    MakeCapsuleMeshAsset(8, 32, 2.0F, std::numeric_limits<float>::max())
      .has_value());
}

NOLINT_TEST(CapsuleTest, RejectsCollapsedFloat32Surfaces)
{
  EXPECT_FALSE(MakeCapsuleMeshAsset(8, 32, 1.0e10F, 0.5F).has_value());
  const auto smallest = std::numeric_limits<float>::denorm_min();
  EXPECT_FALSE(
    MakeCapsuleMeshAsset(8, 32, smallest * 4.0F, smallest).has_value());
  // Merely using large or small units is valid when the surface remains
  // distinct.
  EXPECT_TRUE(MakeCapsuleMeshAsset(8, 32, 1.0e20F, 2.5e19F).has_value());
  EXPECT_TRUE(MakeCapsuleMeshAsset(8, 32, 1.0e-20F, 2.5e-21F).has_value());
}

NOLINT_TEST(
  CapsuleTest, BinaryParametersPreserveDimensionsAndRejectMalformedFields)
{
  struct Parameters {
    uint32_t hemisphere_segments = 4;
    uint32_t radial_segments = 12;
    float height = 3.0F;
    float radius = 0.75F;
  } parameters;
  static_assert(
    sizeof(Parameters) == (2U * sizeof(uint32_t)) + (2U * sizeof(float)));
  const auto bytes = std::as_bytes(std::span(&parameters, 1));
  const auto mesh = GenerateMesh("Capsule/Custom", bytes);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->BoundingBoxMin(), glm::vec3(-0.75F, -0.75F, -1.5F));
  EXPECT_EQ(mesh->BoundingBoxMax(), glm::vec3(0.75F, 0.75F, 1.5F));
  for (auto count = size_t { 0 }; count <= bytes.size(); ++count) {
    EXPECT_EQ(
      GenerateMeshBuffers("Capsule/Prefix", bytes.first(count)).has_value(),
      count % 4U == 0U);
  }
  const auto excess = std::array<std::byte, 20> {};
  EXPECT_FALSE(GenerateMeshBuffers("Capsule/Excess", excess).has_value());
  parameters.height = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(GenerateMeshBuffers("Capsule/NonFinite", bytes).has_value());
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <limits>
#include <string_view>

#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::GenerateMesh;
using oxygen::data::MakeConeMeshAsset;
using oxygen::data::MakeCylinderMeshAsset;
using oxygen::data::MakePlaneMeshAsset;
using oxygen::data::MakeSphereMeshAsset;
using oxygen::data::MakeQuadMeshAsset;
using oxygen::data::MakeTorusMeshAsset;
using oxygen::data::Vertex;
using Buffers = std::pair<std::vector<Vertex>, std::vector<uint32_t>>;

auto ExpectFiniteFrames(const Buffers& buffers) -> void
{
  for (const auto& vertex : buffers.first) {
    for (const auto vector :
      { vertex.position, vertex.normal, vertex.tangent, vertex.bitangent }) {
      EXPECT_TRUE(std::isfinite(vector.x));
      EXPECT_TRUE(std::isfinite(vector.y));
      EXPECT_TRUE(std::isfinite(vector.z));
    }
    EXPECT_NEAR(glm::length(vertex.normal), 1.0F, 1e-6F);
    EXPECT_NEAR(glm::length(vertex.tangent), 1.0F, 1e-6F);
    EXPECT_NEAR(glm::length(vertex.bitangent), 1.0F, 1e-6F);
    EXPECT_NEAR(glm::dot(vertex.normal, vertex.tangent), 0.0F, 1e-6F);
    EXPECT_NEAR(glm::dot(vertex.normal, vertex.bitangent), 0.0F, 1e-6F);
    EXPECT_NEAR(glm::dot(vertex.tangent, vertex.bitangent), 0.0F, 1e-6F);
    EXPECT_GE(vertex.texcoord.x, 0.0F);
    EXPECT_LE(vertex.texcoord.x, 1.0F);
    EXPECT_GE(vertex.texcoord.y, 0.0F);
    EXPECT_LE(vertex.texcoord.y, 1.0F);
  }
}

auto ExpectOutwardTriangles(const Buffers& buffers) -> void
{
  const auto& [vertices, indices] = buffers;
  ASSERT_EQ(indices.size() % 3U, 0U);
  for (size_t index = 0; index < indices.size(); index += 3U) {
    ASSERT_LT(indices.at(index), vertices.size());
    ASSERT_LT(indices.at(index + 1U), vertices.size());
    ASSERT_LT(indices.at(index + 2U), vertices.size());
    const auto& a = vertices.at(indices.at(index));
    const auto& b = vertices.at(indices.at(index + 1U));
    const auto& c = vertices.at(indices.at(index + 2U));
    const auto cross
      = glm::cross(glm::dvec3(b.position) - glm::dvec3(a.position),
        glm::dvec3(c.position) - glm::dvec3(a.position));
    EXPECT_GT(glm::dot(cross, glm::dvec3(a.normal + b.normal + c.normal)), 0.0);
  }
}

NOLINT_TEST(CanonicalPrimitivesTest, PlaneAndQuadHaveDistinctMetricOrientations)
{
  const auto plane = GenerateMesh("Plane/Ground", {});
  const auto quad = GenerateMesh("Quad/Card", {});
  ASSERT_NE(plane, nullptr);
  ASSERT_NE(quad, nullptr);
  EXPECT_EQ(plane->BoundingBoxMin(), glm::vec3(-0.5F, -0.5F, 0.0F));
  EXPECT_EQ(plane->BoundingBoxMax(), glm::vec3(0.5F, 0.5F, 0.0F));
  EXPECT_EQ(quad->BoundingBoxMin(), glm::vec3(-0.5F, 0.0F, -0.5F));
  EXPECT_EQ(quad->BoundingBoxMax(), glm::vec3(0.5F, 0.0F, 0.5F));
  for (const auto& vertex : plane->Vertices()) {
    EXPECT_EQ(vertex.normal, glm::vec3(0, 0, 1));
    EXPECT_EQ(vertex.tangent, glm::vec3(1, 0, 0));
    EXPECT_EQ(vertex.bitangent, glm::vec3(0, -1, 0));
  }
  for (const auto& vertex : quad->Vertices()) {
    EXPECT_EQ(vertex.normal, glm::vec3(0, -1, 0));
    EXPECT_EQ(vertex.tangent, glm::vec3(1, 0, 0));
    EXPECT_EQ(vertex.bitangent, glm::vec3(0, 0, -1));
  }
  const auto quad_buffers = MakeQuadMeshAsset(2.0F, 3.0F);
  const auto plane_buffers = MakePlaneMeshAsset(3, 4, 2.0F);
  if (!quad_buffers.has_value() || !plane_buffers.has_value()) {
    FAIL() << "The valid Quad and Plane recipes must both generate buffers.";
  }
  const auto& quad_data = quad_buffers.value();
  const auto& plane_data = plane_buffers.value();
  ExpectFiniteFrames(quad_data);
  ExpectFiniteFrames(plane_data);
  ExpectOutwardTriangles(quad_data);
  ExpectOutwardTriangles(plane_data);
  // The edge from V=1 to V=0 points opposite the stored increasing-V axis.
  EXPECT_LT(
    glm::dot(quad_data.first.at(3).position - quad_data.first.at(0).position,
      quad_data.first.at(0).bitangent),
    0.0F);
}

NOLINT_TEST(CanonicalPrimitivesTest, TorusDefaultsAndAnalyticFramesAreCanonical)
{
  const auto mesh = GenerateMesh("Torus/Default", {});
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->BoundingBoxMin(), glm::vec3(-0.5F, -0.5F, -0.1F));
  EXPECT_EQ(mesh->BoundingBoxMax(), glm::vec3(0.5F, 0.5F, 0.1F));
  for (const auto major_segments : { 3U, 9U, 32U }) {
    for (const auto minor_segments : { 3U, 7U, 16U }) {
      const auto buffers = MakeTorusMeshAsset(major_segments, minor_segments);
      ASSERT_TRUE(buffers.has_value());
      const auto& data = buffers.value();
      ExpectFiniteFrames(data);
      ExpectOutwardTriangles(data);
      const auto stride = static_cast<size_t>(minor_segments) + 1U;
      for (auto major = 0U; major <= major_segments; ++major) {
        const auto& first = data.first.at(major * stride);
        const auto& last = data.first.at((major * stride) + minor_segments);
        EXPECT_EQ(first.position, last.position);
        EXPECT_EQ(first.normal, last.normal);
        EXPECT_EQ(first.tangent, last.tangent);
        EXPECT_EQ(first.bitangent, last.bitangent);
      }
      for (auto minor = 0U; minor <= minor_segments; ++minor) {
        const auto& first = data.first.at(minor);
        const auto& last = data.first.at((major_segments * stride) + minor);
        EXPECT_EQ(first.position, last.position);
        EXPECT_EQ(first.normal, last.normal);
        EXPECT_EQ(first.tangent, last.tangent);
        EXPECT_EQ(first.bitangent, last.bitangent);
      }
    }
  }
  // Horn and spindle parameterizations retain finite analytic frames even
  // at singular/self-intersecting samples; they are not regular ring tori.
  for (const auto minor_radius : { 1.0F, 2.0F }) {
    const auto buffers = MakeTorusMeshAsset(8, 8, 1.0F, minor_radius);
    ASSERT_TRUE(buffers.has_value());
    ExpectFiniteFrames(*buffers);
  }
}

NOLINT_TEST(CanonicalPrimitivesTest, AxialFramesRespectSurfaceAndUvDirections)
{
  for (const auto segments : { 3U, 7U, 32U }) {
    const auto cylinder = MakeCylinderMeshAsset(segments, 3.0F, 0.75F);
    const auto cone = MakeConeMeshAsset(segments, 3.0F, 0.75F);
    if (!cylinder.has_value() || !cone.has_value()) {
      FAIL()
        << "The valid Cylinder and Cone recipes must both generate buffers.";
    }
    const auto& cylinder_data = cylinder.value();
    const auto& cone_data = cone.value();
    ExpectFiniteFrames(cylinder_data);
    ExpectFiniteFrames(cone_data);
    ExpectOutwardTriangles(cylinder_data);
    ExpectOutwardTriangles(cone_data);
    for (auto index = size_t { 0 }; index <= segments; ++index) {
      EXPECT_EQ(
        cylinder_data.first.at(index * 2U).bitangent, glm::vec3(0, 0, -1));
      const auto& vertex = cone_data.first.at(index);
      EXPECT_GT(glm::dot(vertex.bitangent,
                  glm::vec3(vertex.position.x, vertex.position.y, -3.0F)),
        0.0F);
    }
    EXPECT_EQ(cylinder_data.first.front().position,
      cylinder_data.first.at(static_cast<size_t>(segments) * 2U).position);
    EXPECT_EQ(
      cone_data.first.front().position, cone_data.first.at(segments).position);
  }
  // Computing radius/height in float32 would overflow and corrupt normals.
  const auto flat_cone = MakeConeMeshAsset(8, 1e-20F, 1e20F);
  ASSERT_TRUE(flat_cone.has_value());
  ExpectFiniteFrames(*flat_cone);
}

NOLINT_TEST(CanonicalPrimitivesTest, RejectsInvalidFloatDimensions)
{
  for (const auto invalid :
    { 0.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity() }) {
    EXPECT_FALSE(MakePlaneMeshAsset(1, 1, invalid).has_value());
    EXPECT_FALSE(MakeQuadMeshAsset(invalid, 1.0F).has_value());
    EXPECT_FALSE(MakeQuadMeshAsset(1.0F, invalid).has_value());
    EXPECT_FALSE(MakeCylinderMeshAsset(8, invalid, 0.5F).has_value());
    EXPECT_FALSE(MakeCylinderMeshAsset(8, 1.0F, invalid).has_value());
    EXPECT_FALSE(MakeConeMeshAsset(8, invalid, 0.5F).has_value());
    EXPECT_FALSE(MakeConeMeshAsset(8, 1.0F, invalid).has_value());
    EXPECT_FALSE(MakeTorusMeshAsset(8, 8, invalid, 0.1F).has_value());
    EXPECT_FALSE(MakeTorusMeshAsset(8, 8, 0.4F, invalid).has_value());
  }
}

NOLINT_TEST(
  CanonicalPrimitivesTest, RejectsUnrepresentableRecipesBeforeOverflow)
{
  constexpr auto tiny = std::numeric_limits<float>::denorm_min();
  constexpr auto huge = std::numeric_limits<float>::max();
  constexpr auto count = std::numeric_limits<unsigned int>::max();
  EXPECT_FALSE(MakeQuadMeshAsset(tiny, 1.0F).has_value());
  EXPECT_FALSE(MakePlaneMeshAsset(4, 1, tiny * 2.0F).has_value());
  EXPECT_FALSE(MakeCylinderMeshAsset(32, tiny, 0.5F).has_value());
  EXPECT_FALSE(MakeCylinderMeshAsset(32, 1.0F, tiny).has_value());
  EXPECT_FALSE(MakeConeMeshAsset(32, tiny, 0.5F).has_value());
  EXPECT_FALSE(MakeConeMeshAsset(32, 1.0F, tiny).has_value());
  EXPECT_FALSE(MakeTorusMeshAsset(8, 8, huge, huge).has_value());
  EXPECT_FALSE(MakeTorusMeshAsset(8, 8, 1e20F, 0.5F).has_value());
  EXPECT_FALSE(MakePlaneMeshAsset(count, count).has_value());
  EXPECT_FALSE(MakeSphereMeshAsset(count, count).has_value());
  EXPECT_FALSE(MakeCylinderMeshAsset(count).has_value());
  EXPECT_FALSE(MakeConeMeshAsset(count).has_value());
  EXPECT_FALSE(MakeTorusMeshAsset(count, count).has_value());
}

} // namespace

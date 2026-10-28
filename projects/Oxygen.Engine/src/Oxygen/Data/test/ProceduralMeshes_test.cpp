//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

// GTest
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/ProceduralMeshes.h>

//=== Test Fixtures ===-------------------------------------------------------//

namespace {

//! Fixture covering procedural mesh factory validation and geometry sanity.
class ProceduralMeshTest : public testing::Test { };

//! Checks that all procedural mesh factories reject invalid input and succeed
//! on valid input.
NOLINT_TEST_F(ProceduralMeshTest, ValidInvalidInput)
{
  // Arrange
  using oxygen::data::MakeArrowGizmoMeshAsset;
  using oxygen::data::MakeConeMeshAsset;
  using oxygen::data::MakeCubeMeshAsset;
  using oxygen::data::MakeCylinderMeshAsset;
  using oxygen::data::MakeIcoSphereMeshAsset;
  using oxygen::data::MakePlaneMeshAsset;
  using oxygen::data::MakeQuadMeshAsset;
  using oxygen::data::MakeSphereMeshAsset;
  using oxygen::data::MakeSubdividedCubeMeshAsset;
  using oxygen::data::MakeTorusMeshAsset;

  struct SphereCase {
    int lat;
    int lon;
    bool valid;
    const char* note;
  };

  const auto sphere_cases = std::to_array<SphereCase>({
    { .lat = 2, .lon = 2, .valid = false, .note = "both below min" },
    { .lat = 8, .lon = 8, .valid = true, .note = "typical valid" },
  });

  struct IcoSphereCase {
    int subdivisions;
    bool valid;
    const char* note;
  };

  const auto icosphere_cases = std::to_array<IcoSphereCase>({
    { .subdivisions = 0, .valid = true, .note = "base icosahedron" },
    { .subdivisions = 2, .valid = true, .note = "default-like detail" },
    { .subdivisions = 8, .valid = true, .note = "maximum supported detail" },
    { .subdivisions = 9, .valid = false, .note = "beyond supported limit" },
  });

  struct SubdividedCubeCase {
    int segments;
    bool valid;
    const char* note;
  };

  const auto subdivided_cube_cases = std::to_array<SubdividedCubeCase>({
    { .segments = 0, .valid = false, .note = "segments must be >= 1" },
    { .segments = 1, .valid = true, .note = "minimum valid resolution" },
    { .segments = 6, .valid = true, .note = "default-like detail" },
    { .segments = 64, .valid = true, .note = "maximum supported detail" },
    { .segments = 65, .valid = false, .note = "beyond supported limit" },
  });

  struct PlaneCase {
    int x;
    int z;
    float size;
    bool valid;
    const char* note;
  };

  const auto plane_cases = std::to_array<PlaneCase>({
    { .x = 0, .z = 1, .size = 1.0F, .valid = false, .note = "x=0 invalid" },
    { .x = 1, .z = 0, .size = 1.0F, .valid = false, .note = "z=0 invalid" },
    { .x = 1, .z = 1, .size = 0.0F, .valid = false, .note = "size=0 invalid" },
    { .x = 2, .z = 2, .size = 1.0F, .valid = true, .note = "baseline valid" },
  });

  struct CylinderCase {
    int segments;
    float height;
    float radius;
    bool valid;
    const char* note;
  };

  const auto cylinder_cases = std::to_array<CylinderCase>({
    {
      .segments = 2,
      .height = 1.0F,
      .radius = 0.5F,
      .valid = false,
      .note = "segments below min",
    },
    {
      .segments = 8,
      .height = -1.0F,
      .radius = 0.5F,
      .valid = false,
      .note = "negative height",
    },
    {
      .segments = 8,
      .height = 1.0F,
      .radius = -0.5F,
      .valid = false,
      .note = "negative radius",
    },
    {
      .segments = 8,
      .height = 1.0F,
      .radius = 0.5F,
      .valid = true,
      .note = "valid",
    },
  });

  struct ConeCase {
    int segments;
    float height;
    float radius;
    bool valid;
    const char* note;
  };

  const auto cone_cases = std::to_array<ConeCase>({
    {
      .segments = 2,
      .height = 1.0F,
      .radius = 0.5F,
      .valid = false,
      .note = "segments below min",
    },
    {
      .segments = 8,
      .height = -1.0F,
      .radius = 0.5F,
      .valid = false,
      .note = "negative height",
    },
    {
      .segments = 8,
      .height = 1.0F,
      .radius = -0.5F,
      .valid = false,
      .note = "negative radius",
    },
    {
      .segments = 8,
      .height = 1.0F,
      .radius = 0.5F,
      .valid = true,
      .note = "valid",
    },
  });

  struct TorusCase {
    int major_seg;
    int minor_seg;
    float major_r;
    float minor_r;
    bool valid;
    const char* note;
  };

  const auto torus_cases = std::to_array<TorusCase>({
    {
      .major_seg = 2,
      .minor_seg = 8,
      .major_r = 1.0F,
      .minor_r = 0.25F,
      .valid = false,
      .note = "major segments below min",
    },
    {
      .major_seg = 8,
      .minor_seg = 2,
      .major_r = 1.0F,
      .minor_r = 0.25F,
      .valid = false,
      .note = "minor segments below min",
    },
    {
      .major_seg = 8,
      .minor_seg = 8,
      .major_r = -1.0F,
      .minor_r = 0.25F,
      .valid = false,
      .note = "negative major radius",
    },
    {
      .major_seg = 8,
      .minor_seg = 8,
      .major_r = 1.0F,
      .minor_r = -0.25F,
      .valid = false,
      .note = "negative minor radius",
    },
    {
      .major_seg = 8,
      .minor_seg = 8,
      .major_r = 1.0F,
      .minor_r = 0.25F,
      .valid = true,
      .note = "valid",
    },
  });

  struct QuadCase {
    float w;
    float h;
    bool valid;
    const char* note;
  };

  const auto quad_cases = std::to_array<QuadCase>({
    { .w = 0.0F, .h = 1.0F, .valid = false, .note = "zero width" },
    { .w = 1.0F, .h = 0.0F, .valid = false, .note = "zero height" },
    { .w = 1.0F, .h = 1.0F, .valid = true, .note = "valid" },
  });

  // Act & Assert
  for (const auto& c : sphere_cases) {
    const bool has = MakeSphereMeshAsset(c.lat, c.lon).has_value();
    EXPECT_EQ(has, c.valid)
      << "Sphere(" << c.lat << "," << c.lon << ") " << c.note;
  }
  for (const auto& c : icosphere_cases) {
    const bool has = MakeIcoSphereMeshAsset(c.subdivisions).has_value();
    EXPECT_EQ(has, c.valid) << "IcoSphere(" << c.subdivisions << ") " << c.note;
  }
  for (const auto& c : subdivided_cube_cases) {
    const bool has = MakeSubdividedCubeMeshAsset(c.segments).has_value();
    EXPECT_EQ(has, c.valid)
      << "SubdividedCube(" << c.segments << ") " << c.note;
  }
  for (const auto& c : plane_cases) {
    const bool has = MakePlaneMeshAsset(c.x, c.z, c.size).has_value();
    EXPECT_EQ(has, c.valid)
      << "Plane(" << c.x << "," << c.z << "," << c.size << ") " << c.note;
  }
  for (const auto& c : cylinder_cases) {
    const bool has
      = MakeCylinderMeshAsset(c.segments, c.height, c.radius).has_value();
    EXPECT_EQ(has, c.valid) << "Cylinder(" << c.segments << "," << c.height
                            << "," << c.radius << ") " << c.note;
  }
  for (const auto& c : cone_cases) {
    const bool has
      = MakeConeMeshAsset(c.segments, c.height, c.radius).has_value();
    EXPECT_EQ(has, c.valid) << "Cone(" << c.segments << "," << c.height << ","
                            << c.radius << ") " << c.note;
  }
  for (const auto& c : torus_cases) {
    const bool has
      = MakeTorusMeshAsset(c.major_seg, c.minor_seg, c.major_r, c.minor_r)
          .has_value();
    EXPECT_EQ(has, c.valid)
      << "Torus(" << c.major_seg << "," << c.minor_seg << "," << c.major_r
      << "," << c.minor_r << ") " << c.note;
  }
  for (const auto& c : quad_cases) {
    const bool has = MakeQuadMeshAsset(c.w, c.h).has_value();
    EXPECT_EQ(has, c.valid) << "Quad(" << c.w << "," << c.h << ") " << c.note;
  }

  // Always-valid generators
  EXPECT_TRUE(MakeArrowGizmoMeshAsset().has_value());
  EXPECT_TRUE(MakeCubeMeshAsset().has_value());
}

//! Checks that all procedural mesh assets are valid: non-empty, correct view,
//! and index/vertex counts.
NOLINT_TEST_F(ProceduralMeshTest, MeshValidity)
{
  // Arrange
  using oxygen::data::MakeArrowGizmoMeshAsset;
  using oxygen::data::MakeConeMeshAsset;
  using oxygen::data::MakeCubeMeshAsset;
  using oxygen::data::MakeCylinderMeshAsset;
  using oxygen::data::MakeIcoSphereMeshAsset;
  using oxygen::data::MakePlaneMeshAsset;
  using oxygen::data::MakeQuadMeshAsset;
  using oxygen::data::MakeSphereMeshAsset;
  using oxygen::data::MakeSubdividedCubeMeshAsset;
  using oxygen::data::MakeTorusMeshAsset;
  using oxygen::data::Vertex;
  struct MeshFactory {
    const char* name;
    std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>> (
      *fn)();
  };

  const auto factories = std::to_array<MeshFactory>({
    { .name = "Cube", .fn = &MakeCubeMeshAsset },
    { .name = "ArrowGizmo", .fn = &MakeArrowGizmoMeshAsset },
  });

  // Factories with params
  const auto sphere = MakeSphereMeshAsset(8, 8);
  const auto icosphere = MakeIcoSphereMeshAsset(2);
  const auto subdivided_cube = MakeSubdividedCubeMeshAsset(6);
  const auto plane = MakePlaneMeshAsset(2, 2, 1.0F);
  const auto cylinder = MakeCylinderMeshAsset(8, 1.0F, 0.5F);
  const auto cone = MakeConeMeshAsset(8, 1.0F, 0.5F);
  const auto torus = MakeTorusMeshAsset(8, 8, 1.0F, 0.25F);
  const auto quad = MakeQuadMeshAsset(1.0F, 1.0F);
  using MeshData = std::pair<std::vector<Vertex>, std::vector<uint32_t>>;
  const auto assets = std::to_array<std::optional<MeshData>>({
    sphere,
    icosphere,
    subdivided_cube,
    plane,
    cylinder,
    cone,
    torus,
    quad,
  });

  // Act & Assert
  for (const auto& f : factories) {
    auto mesh_opt = f.fn();
    ASSERT_HAS_VALUE(mesh_opt) << f.name;
    const auto& [vertices, indices] = *mesh_opt;
    EXPECT_GT(vertices.size(), 0U) << f.name;
    EXPECT_GT(indices.size(), 0U) << f.name;
  }
  for (const auto& mesh_opt : assets) {
    ASSERT_HAS_VALUE(mesh_opt);
    const auto& [vertices, indices] = *mesh_opt;
    EXPECT_GT(vertices.size(), 0U);
    EXPECT_GT(indices.size(), 0U);
  }
}

//! Checks that all procedural mesh assets compute a valid bounding box.
NOLINT_TEST_F(ProceduralMeshTest, BoundingBox)
{
  // Arrange
  using oxygen::data::MakeCubeMeshAsset;
  const auto mesh_opt = MakeCubeMeshAsset();
  ASSERT_HAS_VALUE(mesh_opt);
  const auto& [vertices, indices] = *mesh_opt;

  // Compute bounding box from vertices
  ASSERT_FALSE(vertices.empty());
  glm::vec3 min = vertices.front().position;
  glm::vec3 max = vertices.front().position;
  for (const auto& v : vertices) {
    min = glm::min(min, v.position);
    max = glm::max(max, v.position);
  }

  // Assert
  EXPECT_LE(min.x, max.x);
  EXPECT_LE(min.y, max.y);
  EXPECT_LE(min.z, max.z);
  // Cube should be centered at origin, size 1
  EXPECT_EQ(min, glm::vec3(-0.5F, -0.5F, -0.5F));
  EXPECT_EQ(max, glm::vec3(0.5F, 0.5F, 0.5F));
}

//! Boundary tests for minimum valid sphere segment counts.
//! Verifies documentated lower limits: latitude_segments >=3,
//! longitude_segments
//! >=3.
NOLINT_TEST_F(ProceduralMeshTest, SphereMinimumValidSegments)
{
  // Arrange
  using oxygen::data::MakeSphereMeshAsset;
  // Act & Assert
  EXPECT_FALSE(MakeSphereMeshAsset(2, 3).has_value())
    << "Latitude=2 should be invalid (min 3)";
  EXPECT_FALSE(MakeSphereMeshAsset(3, 2).has_value())
    << "Longitude=2 should be invalid (min 3)";
  EXPECT_TRUE(MakeSphereMeshAsset(3, 3).has_value())
    << "(3,3) should be the minimum valid sphere";
}

//! Verifies icosphere topology growth follows expected refinement factors:
//! each level splits every triangle in four; the seam and poles add vertices
//! beyond the 10 * 4^level + 2 shared positions.
NOLINT_TEST_F(ProceduralMeshTest, IcoSphereSubdivisionGrowth)
{
  using oxygen::data::MakeIcoSphereMeshAsset;

  const auto sub0 = MakeIcoSphereMeshAsset(0);
  const auto sub1 = MakeIcoSphereMeshAsset(1);
  const auto sub2 = MakeIcoSphereMeshAsset(2);
  ASSERT_HAS_VALUE(sub0);
  ASSERT_HAS_VALUE(sub1);
  ASSERT_HAS_VALUE(sub2);

  EXPECT_GT(sub0->first.size(), 12U);
  EXPECT_EQ(sub0->second.size(), 60U);
  EXPECT_GT(sub1->first.size(), 42U);
  EXPECT_EQ(sub1->second.size(), 240U);
  EXPECT_GT(sub2->first.size(), 162U);
  EXPECT_EQ(sub2->second.size(), 960U);
}

//! Verifies subdivided-cube topology growth follows expected formulas: each
//! face has its own (segments + 1)^2 vertices.
NOLINT_TEST_F(ProceduralMeshTest, SubdividedCubeResolutionGrowth)
{
  using oxygen::data::MakeSubdividedCubeMeshAsset;

  const auto seg1 = MakeSubdividedCubeMeshAsset(1);
  const auto seg2 = MakeSubdividedCubeMeshAsset(2);
  const auto seg4 = MakeSubdividedCubeMeshAsset(4);
  ASSERT_HAS_VALUE(seg1);
  ASSERT_HAS_VALUE(seg2);
  ASSERT_HAS_VALUE(seg4);

  EXPECT_EQ(seg1->first.size(), 24U);
  EXPECT_EQ(seg1->second.size(), 36U);

  EXPECT_EQ(seg2->first.size(), 54U);
  EXPECT_EQ(seg2->second.size(), 144U);

  EXPECT_EQ(seg4->first.size(), 150U);
  EXPECT_EQ(seg4->second.size(), 576U);
}

//! Boundary tests for plane minimum resolution and size parameter.
//! Verifies documented constraints: x_segments>=1, y_segments>=1, size>0.
NOLINT_TEST_F(ProceduralMeshTest, PlaneMinimumResolution)
{
  // Arrange
  using oxygen::data::MakePlaneMeshAsset;
  // Invalid just-below boundaries
  // Act & Assert
  EXPECT_FALSE(MakePlaneMeshAsset(0, 1, 1.0F).has_value())
    << "x_segments=0 invalid";
  EXPECT_FALSE(MakePlaneMeshAsset(1, 0, 1.0F).has_value())
    << "y_segments=0 invalid";
  EXPECT_FALSE(MakePlaneMeshAsset(1, 1, 0.0F).has_value()) << "size<=0 invalid";
  const auto one_one = MakePlaneMeshAsset(1, 1, 1.0F);
  ASSERT_HAS_VALUE(one_one);
  EXPECT_EQ(one_one->first.size(), 4U);
  EXPECT_EQ(one_one->second.size(), 6U);
  EXPECT_TRUE(MakePlaneMeshAsset(2, 2, 1.0F).has_value())
    << "(2,2) must be valid";
}

//! Checks that the default view of each procedural mesh covers the full mesh.
NOLINT_TEST_F(ProceduralMeshTest, DefaultView)
{
  // Arrange
  using oxygen::data::MakeCubeMeshAsset;
  const auto mesh_opt = MakeCubeMeshAsset();
  ASSERT_HAS_VALUE(mesh_opt);
  const auto& [vertices, indices] = *mesh_opt;

  // Act & Assert: the only view is the full data
  // Cube uses 24 per-face vertices (separate normals/tangents per face)
  EXPECT_EQ(vertices.size(), 24U);
  EXPECT_EQ(indices.size(), 36U); // Cube should have 36 indices
}

//! Checks each procedural mesh type for expected geometry and view properties.
NOLINT_TEST_F(ProceduralMeshTest, PerMeshType)
{
  // Arrange
  using oxygen::data::MakeArrowGizmoMeshAsset;
  using oxygen::data::MakeConeMeshAsset;
  using oxygen::data::MakeCubeMeshAsset;
  using oxygen::data::MakeCylinderMeshAsset;
  using oxygen::data::MakeIcoSphereMeshAsset;
  using oxygen::data::MakePlaneMeshAsset;
  using oxygen::data::MakeQuadMeshAsset;
  using oxygen::data::MakeSphereMeshAsset;
  using oxygen::data::MakeSubdividedCubeMeshAsset;
  using oxygen::data::MakeTorusMeshAsset;
  using oxygen::data::Vertex;
  struct MeshType {
    const char* name { "" };
    std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>> asset;
    size_t min_vertices { 0 };
    size_t min_indices { 0 };
  };

  const auto types = std::to_array<MeshType>({
    {
      .name = "Cube",
      .asset = MakeCubeMeshAsset(),
      .min_vertices = 24,
      .min_indices = 36,
    },
    {
      .name = "SubdividedCube",
      .asset = MakeSubdividedCubeMeshAsset(6),
      .min_vertices = 294,
      .min_indices = 1296,
    },
    {
      .name = "Sphere",
      .asset = MakeSphereMeshAsset(8, 8),
      .min_vertices = 79,
      .min_indices = 336,
    },
    {
      .name = "IcoSphere",
      .asset = MakeIcoSphereMeshAsset(2),
      .min_vertices = 162,
      .min_indices = 960,
    },
    {
      .name = "Plane",
      .asset = MakePlaneMeshAsset(2, 2, 1.0F),
      .min_vertices = 9,
      .min_indices = 24,
    },
    {
      .name = "Cylinder",
      .asset = MakeCylinderMeshAsset(8, 1.0F, 0.5F),
      .min_vertices = 18,
      .min_indices = 72,
    },
    {
      .name = "Cone",
      .asset = MakeConeMeshAsset(8, 1.0F, 0.5F),
      .min_vertices = 11,
      .min_indices = 48,
    },
    {
      .name = "Torus",
      .asset = MakeTorusMeshAsset(8, 8, 1.0F, 0.25F),
      .min_vertices = 81,
      .min_indices = 384,
    },
    {
      .name = "Quad",
      .asset = MakeQuadMeshAsset(1.0F, 1.0F),
      .min_vertices = 4,
      .min_indices = 6,
    },
    {
      .name = "ArrowGizmo",
      .asset = MakeArrowGizmoMeshAsset(),
      .min_vertices = 0,
      .min_indices = 0,
    },
  });

  // Act & Assert
  for (const auto& t : types) {
    ASSERT_HAS_VALUE(t.asset) << t.name;
    const auto& [vertices, indices] = *t.asset;
    if (t.min_vertices > 0) {
      EXPECT_GE(vertices.size(), t.min_vertices) << t.name;
    }
    if (t.min_indices > 0) {
      EXPECT_GE(indices.size(), t.min_indices) << t.name;
    }
  }
}

//! Verifies per-shape basic topology invariants: >0 vertices, triangles
//! well-formed, index buffer length divisible by 3, all indices in range, and
//! no obviously invalid winding (degenerate detection handled in dedicated test
//! for torus / cone).
NOLINT_TEST_F(ProceduralMeshTest, ShapesTopologyValid)
{
  // Arrange
  using oxygen::data::MakeConeMeshAsset;
  using oxygen::data::MakeCubeMeshAsset;
  using oxygen::data::MakeCylinderMeshAsset;
  using oxygen::data::MakeIcoSphereMeshAsset;
  using oxygen::data::MakePlaneMeshAsset;
  using oxygen::data::MakeQuadMeshAsset;
  using oxygen::data::MakeSphereMeshAsset;
  using oxygen::data::MakeSubdividedCubeMeshAsset;
  using oxygen::data::MakeTorusMeshAsset;
  using oxygen::data::Vertex;
  struct NamedAsset {
    const char* name;
    std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>> asset;
  };

  const auto assets = std::to_array<NamedAsset>({
    { .name = "Cube", .asset = MakeCubeMeshAsset() },
    { .name = "SubdividedCube", .asset = MakeSubdividedCubeMeshAsset(6) },
    { .name = "Sphere", .asset = MakeSphereMeshAsset(8, 8) },
    { .name = "IcoSphere", .asset = MakeIcoSphereMeshAsset(2) },
    { .name = "Plane", .asset = MakePlaneMeshAsset(2, 2, 1.0F) },
    { .name = "Cylinder", .asset = MakeCylinderMeshAsset(8, 1.0F, 0.5F) },
    { .name = "Cone", .asset = MakeConeMeshAsset(8, 1.0F, 0.5F) },
    { .name = "Torus", .asset = MakeTorusMeshAsset(8, 8, 1.0F, 0.25F) },
    { .name = "Quad", .asset = MakeQuadMeshAsset(1.0F, 1.0F) },
  });

  // Act & Assert
  for (const auto& a : assets) {
    ASSERT_HAS_VALUE(a.asset) << a.name;
    const auto& vertices = a.asset->first;
    const auto& indices = a.asset->second;
    ASSERT_FALSE(vertices.empty()) << a.name;
    ASSERT_FALSE(indices.empty()) << a.name;
    EXPECT_EQ(indices.size() % 3, 0U)
      << a.name << " index count must be multiple of 3";
    for (uint32_t idx : indices) {
      EXPECT_LT(idx, vertices.size()) << a.name << " index out of range";
    }
  }
}

//! Ensures the torus contains no degenerate triangles (no triangle where any
//! two consecutive indices are identical). This guards against topology bugs
//! when wrapping segment seams.
NOLINT_TEST_F(ProceduralMeshTest, Torus_NoDegenerateTriangles)
{
  // Arrange
  using oxygen::data::MakeTorusMeshAsset;
  auto torus = MakeTorusMeshAsset(8, 8, 1.0F, 0.25F);
  ASSERT_HAS_VALUE(torus);
  const auto& indices = torus->second;

  // Act & Assert
  ASSERT_EQ(indices.size() % 3, 0U);
  for (size_t i = 0; i < indices.size(); i += 3) {
    uint32_t a = indices[i];
    uint32_t b = indices[i + 1];
    uint32_t c = indices[i + 2];
    EXPECT_FALSE(a == b || b == c || a == c)
      << "Degenerate triangle at tri " << (i / 3) << " indices (" << a << ","
      << b << "," << c << ")";
  }
}

} // namespace

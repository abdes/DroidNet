//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/ext/vector_double2.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/ProceduralMeshDefaults.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>

//! Every procedural shape against the engine contracts it must obey:
//! `clip::FrontFaceCCW` winding, `space::texture` tangent frames (+dP/dU,
//! +dP/dV) and its "UV layout of generated surfaces", closed shapes watertight
//! once welded by position, and enclosed volumes that match the analytic
//! solid. Expected directions come from `space::move`, never from literals.

namespace {

using oxygen::Vec3;
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
namespace procedural = oxygen::data::procedural;
namespace move = oxygen::space::move;

using Buffers = std::pair<std::vector<Vertex>, std::vector<uint32_t>>;

constexpr double kPi = std::numbers::pi_v<double>;

//! One shape and recipe, and what the contract says about it.
struct ShapeCase final {
  std::string_view name;
  std::function<std::optional<Buffers>()> make;
  //! Closed surface: watertight after welding, with an enclosed volume.
  bool closed = false;
  //! Closed parts after welding by position.
  std::size_t components = 1;
  //! The analytic solid's volume; zero for open shapes.
  double volume = 0.0;
  //! Flat faces whose normal is +Z or -Z follow the top and bottom layouts.
  bool flat_caps = false;
  //! Tessellation fine enough that each corner's frame lies within
  //! acos(frame_cosine) of its triangle's dP/dU and dP/dV, and the volume
  //! within 5% of the solid's. A coarse recipe's triangles span so much of the
  //! surface that their linear UVs say little about the smooth frame; only its
  //! layout and topology are checked.
  bool fine = false;
  double frame_cosine = 0.9;
  //! Seam triangles may continue past u = 1 (samplers wrap).
  double max_u = 1.0;
};

[[nodiscard]] auto Capsule(const unsigned hemisphere, const unsigned radial,
  const float height, const float radius) -> ShapeCase
{
  const auto r = static_cast<double>(radius);
  return {
    .name = "Capsule",
    .make = [=] -> std::optional<Buffers> {
      return oxygen::data::MakeCapsuleMeshAsset(
        hemisphere, radial, height, radius);
    },
    .closed = true,
    .volume
    = (kPi * r * r * (height - (2.0 * r))) + (4.0 / 3.0 * kPi * r * r * r),
    .fine = hemisphere >= 8U && radial >= 32U,
  };
}

[[nodiscard]] auto AllShapes() -> std::vector<ShapeCase>
{
  constexpr double kSphereVolume = 4.0 / 3.0 * kPi * 0.125;
  return {
    {
      .name = "Cube",
      .make = [] -> std::optional<Buffers> { return MakeCubeMeshAsset(); },
      .closed = true,
      .volume = 1.0,
      .flat_caps = true,
      .fine = true,
    },
    {
      .name = "SubdividedCube(3)",
      .make = [] -> std::optional<Buffers> {
        return MakeSubdividedCubeMeshAsset(3U);
      },
      .closed = true,
      .volume = 1.0,
      .flat_caps = true,
      .fine = true,
    },
    {
      .name = "SubdividedCube(default)",
      .make
      = [] -> std::optional<Buffers> { return MakeSubdividedCubeMeshAsset(); },
      .closed = true,
      .volume = 1.0,
      .flat_caps = true,
      .fine = true,
    },
    {
      .name = "Sphere(default)",
      .make = [] -> std::optional<Buffers> { return MakeSphereMeshAsset(); },
      .closed = true,
      .volume = kSphereVolume,
      .fine = true,
    },
    {
      .name = "Sphere(3, 3)",
      .make
      = [] -> std::optional<Buffers> { return MakeSphereMeshAsset(3U, 3U); },
      .closed = true,
      .volume = kSphereVolume,
    },
    {
      .name = "IcoSphere(default)",
      .make = [] -> std::optional<Buffers> { return MakeIcoSphereMeshAsset(); },
      .closed = true,
      .volume = kSphereVolume,
      .fine = true,
      // Icosahedral triangles cut across the latitude-longitude layout, so
      // their linear UVs drift up to about 31 degrees from the smooth frame.
      .frame_cosine = 0.85,
      .max_u = 1.5,
    },
    {
      .name = "IcoSphere(0)",
      .make
      = [] -> std::optional<Buffers> { return MakeIcoSphereMeshAsset(0U); },
      .closed = true,
      .volume = kSphereVolume,
      .max_u = 1.5,
    },
    Capsule(procedural::kCapsuleHemisphereSegments,
      procedural::kCapsuleRadialSegments, procedural::kCapsuleHeight,
      procedural::kCapsuleRadius),
    Capsule(1U, 3U, 1.0F, 0.5F),
    Capsule(3U, 9U, 4.0F, 0.5F),
    {
      .name = "Cylinder(default)",
      .make = [] -> std::optional<Buffers> { return MakeCylinderMeshAsset(); },
      .closed = true,
      .volume = kPi * 0.25 * 1.0,
      .flat_caps = true,
      .fine = true,
    },
    {
      .name = "Cylinder(3)",
      .make = [] -> std::optional<Buffers> {
        return MakeCylinderMeshAsset(3U, 3.0F, 0.75F);
      },
      .closed = true,
      .volume = kPi * 0.5625 * 3.0,
      .flat_caps = true,
    },
    {
      .name = "Cone(default)",
      .make = [] -> std::optional<Buffers> { return MakeConeMeshAsset(); },
      .closed = true,
      .volume = kPi * 0.25 * 1.0 / 3.0,
      .flat_caps = true,
      .fine = true,
    },
    {
      .name = "Cone(3)",
      .make = [] -> std::optional<Buffers> {
        return MakeConeMeshAsset(3U, 3.0F, 0.75F);
      },
      .closed = true,
      .volume = kPi * 0.5625 * 3.0 / 3.0,
      .flat_caps = true,
    },
    {
      .name = "Torus(default)",
      .make = [] -> std::optional<Buffers> { return MakeTorusMeshAsset(); },
      .closed = true,
      .volume = 2.0 * kPi * kPi * 0.4 * 0.01,
      .fine = true,
    },
    {
      .name = "Torus(3, 3)",
      .make = [] -> std::optional<Buffers> {
        return MakeTorusMeshAsset(3U, 3U, 0.4F, 0.1F);
      },
      .closed = true,
      .volume = 2.0 * kPi * kPi * 0.4 * 0.01,
    },
    {
      .name = "Plane(default)",
      .make = [] -> std::optional<Buffers> { return MakePlaneMeshAsset(); },
      .flat_caps = true,
      .fine = true,
    },
    {
      .name = "Plane(3, 4)",
      .make = [] -> std::optional<Buffers> {
        return MakePlaneMeshAsset(3U, 4U, 2.0F);
      },
      .flat_caps = true,
      .fine = true,
    },
    {
      .name = "Quad",
      .make
      = [] -> std::optional<Buffers> { return MakeQuadMeshAsset(2.0F, 3.0F); },
      .fine = true,
    },
    {
      .name = "ArrowGizmo",
      .make
      = [] -> std::optional<Buffers> { return MakeArrowGizmoMeshAsset(); },
      .closed = true,
      // The shaft's top cap and the head's base cap share their centre, so
      // the two closed solids weld into one piece at that point.
      .components = 1,
      .volume = (kPi * 0.025 * 0.025 * 0.7) + (kPi * 0.06 * 0.06 * 0.18 / 3.0),
      .flat_caps = true,
      .fine = true,
    },
  };
}

//! Whether two unit vectors point the same way within `cosine`.
[[nodiscard]] auto Along(const Vec3& a, const Vec3& b, const double cosine)
  -> bool
{
  return glm::dot(glm::dvec3(a), glm::dvec3(b)) >= cosine;
}

//! The sign an unmirrored frame has: the front face's T = Right, B = Down
//! seen from its normal, Forward.
[[nodiscard]] auto UnmirroredSign() -> float
{
  return glm::dot(glm::cross(move::Forward, move::Right), move::Down);
}

auto ExpectOrthonormalUnmirroredFrames(
  const ShapeCase& shape, const std::vector<Vertex>& vertices) -> void
{
  constexpr float kTolerance = 1.0e-5F;
  const auto unmirrored = UnmirroredSign();
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const auto& vertex = vertices[i];
    SCOPED_TRACE(testing::Message() << "vertex " << i);
    EXPECT_NEAR(glm::length(vertex.normal), 1.0F, kTolerance);
    EXPECT_NEAR(glm::length(vertex.tangent), 1.0F, kTolerance);
    EXPECT_NEAR(glm::length(vertex.bitangent), 1.0F, kTolerance);
    EXPECT_NEAR(glm::dot(vertex.normal, vertex.tangent), 0.0F, kTolerance);
    EXPECT_NEAR(glm::dot(vertex.normal, vertex.bitangent), 0.0F, kTolerance);
    EXPECT_NEAR(glm::dot(vertex.tangent, vertex.bitangent), 0.0F, kTolerance);
    EXPECT_GT(
      glm::dot(glm::cross(vertex.normal, vertex.tangent), vertex.bitangent)
        * unmirrored,
      0.999F);
    EXPECT_GE(vertex.texcoord.x, 0.0F);
    EXPECT_LE(vertex.texcoord.x, static_cast<float>(shape.max_u));
    EXPECT_GE(vertex.texcoord.y, 0.0F);
    EXPECT_LE(vertex.texcoord.y, 1.0F);
  }
}

//! "UV layout of generated surfaces": upright on vertical outer walls, the
//! top and bottom layouts on flat caps.
auto ExpectUprightLayout(
  const ShapeCase& shape, const std::vector<Vertex>& vertices) -> void
{
  constexpr double kCosine = 0.99;
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const auto& vertex = vertices[i];
    SCOPED_TRACE(testing::Message() << "vertex " << i);
    const auto horizontal = Vec3 { vertex.normal.x, vertex.normal.y, 0.0F };
    const auto outward = glm::dot(horizontal,
                           Vec3 { vertex.position.x, vertex.position.y, 0.0F })
      > 0.0F;
    if (std::abs(vertex.normal.z) < 0.1F && outward) {
      EXPECT_TRUE(Along(vertex.tangent,
        glm::normalize(glm::cross(move::Up, vertex.normal)), kCosine));
      EXPECT_TRUE(Along(vertex.bitangent, move::Down, kCosine));
    }
    if (Along(vertex.normal, move::Forward, 0.9999)
      && glm::dot(
           horizontal, Vec3 { vertex.position.x, vertex.position.y, 0.0F })
        >= 0.0F) {
      EXPECT_TRUE(Along(vertex.tangent, move::Right, kCosine));
      EXPECT_TRUE(Along(vertex.bitangent, move::Down, kCosine));
    }
    if (shape.flat_caps && Along(vertex.normal, move::Up, 0.9999)) {
      EXPECT_TRUE(Along(vertex.tangent, move::Right, kCosine));
      EXPECT_TRUE(Along(vertex.bitangent, move::Forward, kCosine));
    }
    if (shape.flat_caps && Along(vertex.normal, move::Down, 0.9999)) {
      EXPECT_TRUE(Along(vertex.tangent, move::Right, kCosine));
      EXPECT_TRUE(Along(vertex.bitangent, move::Back, kCosine));
    }
  }
}

//! Each triangle winds counter-clockwise seen from the side its normals point
//! to, is not degenerate, has an unmirrored UV layout, and its corners'
//! frames follow its dP/dU and dP/dV.
auto ExpectTrianglesFollowContract(const ShapeCase& shape,
  const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
  -> void
{
  const auto alignment = shape.frame_cosine;
  const auto unmirrored = static_cast<double>(UnmirroredSign());
  ASSERT_EQ(indices.size() % 3U, 0U);
  for (std::size_t t = 0; t < indices.size(); t += 3U) {
    SCOPED_TRACE(testing::Message() << "triangle " << (t / 3U));
    std::array<const Vertex*, 3> corner {};
    for (std::size_t k = 0; k < 3U; ++k) {
      ASSERT_LT(indices[t + k], vertices.size());
      corner.at(k) = &vertices[indices[t + k]];
    }
    const auto p0 = glm::dvec3(corner[0]->position);
    const auto e1 = glm::dvec3(corner[1]->position) - p0;
    const auto e2 = glm::dvec3(corner[2]->position) - p0;
    const auto face = glm::cross(e1, e2);
    ASSERT_GT(glm::length(face), 1.0e-12) << "degenerate triangle";
    const auto normals
      = glm::dvec3(corner[0]->normal + corner[1]->normal + corner[2]->normal);
    EXPECT_GT(glm::dot(face, normals), 0.0) << "winds inward";

    const auto uv0 = glm::dvec2(corner[0]->texcoord);
    const auto d1 = glm::dvec2(corner[1]->texcoord) - uv0;
    const auto d2 = glm::dvec2(corner[2]->texcoord) - uv0;
    const auto det = (d1.x * d2.y) - (d1.y * d2.x);
    ASSERT_GT(std::abs(det), 1.0e-12) << "degenerate UVs";
    const auto dp_du = ((e1 * d2.y) - (e2 * d1.y)) / det;
    const auto dp_dv = ((e2 * d1.x) - (e1 * d2.x)) / det;
    EXPECT_GT(glm::dot(glm::cross(face, dp_du), dp_dv) * unmirrored, 0.0)
      << "mirrored UV layout";

    if (!shape.fine) {
      continue;
    }
    for (const auto* vertex : corner) {
      const auto n = glm::dvec3(vertex->normal);
      const auto along_u = glm::normalize(dp_du - (n * glm::dot(n, dp_du)));
      const auto along_v = glm::normalize(dp_dv - (n * glm::dot(n, dp_dv)));
      EXPECT_GT(glm::dot(glm::dvec3(vertex->tangent), along_u), alignment);
      EXPECT_GT(glm::dot(glm::dvec3(vertex->bitangent), along_v), alignment);
    }
  }
}

//! A position's exact value, for welding; -0 and +0 are one value.
[[nodiscard]] auto PositionKey(const Vec3& position)
  -> std::tuple<uint32_t, uint32_t, uint32_t>
{
  return {
    std::bit_cast<uint32_t>(position.x + 0.0F),
    std::bit_cast<uint32_t>(position.y + 0.0F),
    std::bit_cast<uint32_t>(position.z + 0.0F),
  };
}

//! Welded by exact position, a closed shape is a 2-manifold: every edge has
//! exactly one twin running the other way, in `shape.components` parts, and
//! encloses its solid's volume.
auto ExpectWatertight(const ShapeCase& shape,
  const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
  -> void
{
  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, std::size_t> welded_ids;
  std::vector<std::size_t> welded(vertices.size());
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    welded[i]
      = welded_ids
          .try_emplace(PositionKey(vertices[i].position), welded_ids.size())
          .first->second;
  }

  std::vector<std::size_t> parent(welded_ids.size());
  std::ranges::iota(parent, std::size_t { 0 });
  const auto root = [&parent](std::size_t id) -> std::size_t {
    while (parent[id] != id) {
      parent[id] = parent[parent[id]];
      id = parent[id];
    }
    return id;
  };

  std::map<std::pair<std::size_t, std::size_t>, int> directed_edges;
  auto volume = 0.0;
  for (std::size_t t = 0; t < indices.size(); t += 3U) {
    const std::array ids {
      welded[indices[t]],
      welded[indices[t + 1U]],
      welded[indices[t + 2U]],
    };
    ASSERT_NE(ids[0], ids[1]);
    ASSERT_NE(ids[1], ids[2]);
    ASSERT_NE(ids[2], ids[0]);
    for (std::size_t k = 0; k < 3U; ++k) {
      const auto from = ids.at(k);
      const auto to = ids.at((k + 1U) % 3U);
      ++directed_edges[{ from, to }];
      parent[root(from)] = root(to);
    }
    volume += glm::dot(glm::dvec3(vertices[indices[t]].position),
                glm::cross(glm::dvec3(vertices[indices[t + 1U]].position),
                  glm::dvec3(vertices[indices[t + 2U]].position)))
      / 6.0;
  }

  for (const auto& [edge, count] : directed_edges) {
    EXPECT_EQ(count, 1) << "edge used twice in one direction";
    const auto twin = directed_edges.find({ edge.second, edge.first });
    EXPECT_TRUE(twin != directed_edges.end() && twin->second == 1)
      << "open edge " << edge.first << " -> " << edge.second;
  }

  std::size_t components = 0;
  for (std::size_t id = 0; id < parent.size(); ++id) {
    components += root(id) == id ? 1U : 0U;
  }
  EXPECT_EQ(components, shape.components);

  EXPECT_GT(volume, 0.0) << "encloses negative volume: wound inward";
  EXPECT_LE(volume, shape.volume * 1.0001) << "larger than the solid";
  if (shape.fine) {
    EXPECT_GE(volume, shape.volume * 0.95) << "far smaller than the solid";
  }
}

//! Every shape obeys the contracts, for default and coarse recipes alike.
NOLINT_TEST(ProceduralConformanceTest, EveryShapeFollowsTheEngineContracts)
{
  for (const auto& shape : AllShapes()) {
    SCOPED_TRACE(shape.name);
    const auto buffers = shape.make();
    ASSERT_HAS_VALUE(buffers);
    const auto& [vertices, indices] = *buffers;
    ASSERT_FALSE(vertices.empty());
    ASSERT_FALSE(indices.empty());

    ExpectOrthonormalUnmirroredFrames(shape, vertices);
    ExpectUprightLayout(shape, vertices);
    ExpectTrianglesFollowContract(shape, vertices, indices);
    if (shape.closed) {
      ExpectWatertight(shape, vertices, indices);
    }
  }
}

//! The same recipe always produces the same bytes.
NOLINT_TEST(ProceduralConformanceTest, EveryShapeIsDeterministic)
{
  for (const auto& shape : AllShapes()) {
    SCOPED_TRACE(shape.name);
    const auto first = shape.make();
    const auto second = shape.make();
    ASSERT_HAS_VALUE(first);
    ASSERT_HAS_VALUE(second);
    ASSERT_EQ(first->first.size(), second->first.size());
    EXPECT_EQ(std::memcmp(first->first.data(), second->first.data(),
                first->first.size() * sizeof(Vertex)),
      0);
    EXPECT_EQ(first->second, second->second);
  }
}

} // namespace

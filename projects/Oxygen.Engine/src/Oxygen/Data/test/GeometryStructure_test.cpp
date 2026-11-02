//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Content/Loaders/GeometryLoader.h, Data/GeometryAsset.h

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/ProductRefusal.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::GeometryBuilder;
using oxygen::cooker::testing::GeometryRefusal;
using oxygen::data::CookedDefect;
using oxygen::data::SurfaceId;
using oxygen::data::pak::geometry::LodPolicyRecord;
using oxygen::data::pak::geometry::LodPolicyType;
using oxygen::data::pak::geometry::MeshViewDesc;
using oxygen::data::pak::geometry::SurfaceDesc;

constexpr auto kLast = std::numeric_limits<uint32_t>::max();

//! A geometry draws views its descriptor checks before any buffer is bound,
//! and declares the defaults of exactly the surfaces its submeshes name.
class GeometryStructureTest : public testing::Test {
protected:
  //! A standard geometry whose two surfaces each draw one view of four
  //! vertices and six indices.
  [[nodiscard]] static auto Geometry() -> std::vector<std::byte>
  {
    constexpr auto kView = MeshViewDesc {
      .first_index = 0,
      .index_count = 6,
      .first_vertex = 0,
      .vertex_count = 4,
    };
    auto geometry = GeometryBuilder { "Quad" };
    const auto lod = geometry.StandardLod(oxygen::ResourceIndexT { 1U },
      oxygen::ResourceIndexT { 2U }, glm::vec3 { 0.0F }, glm::vec3 { 1.0F });
    geometry.Submesh(lod, Surface("front"), {}, { kView });
    geometry.Submesh(lod, Surface("back"), {}, { kView });
    return geometry.Bytes();
  }

  [[nodiscard]] static auto Surface(const char* name) -> SurfaceId
  {
    return SurfaceId::FromStableIdentity(
      std::string("GeometryStructure/") + name);
  }

  //! `Geometry()` with its first view changed by `edit`.
  template <typename Edit>
  [[nodiscard]] static auto WithView(const Edit& edit) -> std::vector<std::byte>
  {
    auto geometry = Geometry();
    auto view = DescriptorDefects::MeshViewAt(geometry, 0);
    edit(view);
    DescriptorDefects::SetMeshView(geometry, 0, view);
    return geometry;
  }

  //! `Geometry()` with the defaults of its second surface changed by `edit`.
  template <typename Edit>
  [[nodiscard]] static auto WithSurfaceDefaults(const Edit& edit)
    -> std::vector<std::byte>
  {
    auto geometry = Geometry();
    auto surface = DescriptorDefects::SurfaceDefaultsAt(geometry, 1);
    edit(surface);
    DescriptorDefects::SetSurfaceDefaults(geometry, 1, surface);
    return geometry;
  }
};

NOLINT_TEST_F(GeometryStructureTest, ReadsAWellFormedGeometry)
{
  EXPECT_EQ(GeometryRefusal(Geometry()), std::nullopt);
}

//! A view draws at least one vertex and one index.
NOLINT_TEST_F(GeometryStructureTest, RefusesAnEmptyView)
{
  EXPECT_EQ(GeometryRefusal(WithView(
              [](MeshViewDesc& view) -> void { view.vertex_count = 0; })),
    CookedDefect::kInvalidValue);
  EXPECT_EQ(GeometryRefusal(WithView(
              [](MeshViewDesc& view) -> void { view.index_count = 0; })),
    CookedDefect::kInvalidValue);
}

//! A view ends within the 32-bit range of its buffer's elements.
NOLINT_TEST_F(GeometryStructureTest, RefusesAViewPastTheElementRange)
{
  EXPECT_EQ(GeometryRefusal(WithView(
              [](MeshViewDesc& view) -> void { view.first_vertex = kLast; })),
    CookedDefect::kOutOfBounds);
  EXPECT_EQ(GeometryRefusal(WithView(
              [](MeshViewDesc& view) -> void { view.first_index = kLast; })),
    CookedDefect::kOutOfBounds);
}

//! Every declared surface has its defaults.
NOLINT_TEST_F(GeometryStructureTest, RefusesASurfaceWithoutDefaults)
{
  auto geometry = Geometry();
  DescriptorDefects::DropSurfaceDefaults(geometry);

  EXPECT_EQ(GeometryRefusal(geometry), CookedDefect::kInconsistent);
}

//! Defaults name a surface the geometry's submeshes declare.
NOLINT_TEST_F(GeometryStructureTest, RefusesDefaultsOfAnUndeclaredSurface)
{
  EXPECT_EQ(
    GeometryRefusal(WithSurfaceDefaults([](SurfaceDesc& surface) -> void {
      surface.surface_id = Surface("other");
    })),
    CookedDefect::kInconsistent);
}

//! A surface has one set of defaults.
NOLINT_TEST_F(GeometryStructureTest, RefusesTwoDefaultsOfOneSurface)
{
  EXPECT_EQ(
    GeometryRefusal(WithSurfaceDefaults([](SurfaceDesc& surface) -> void {
      surface.surface_id = Surface("front");
    })),
    CookedDefect::kDuplicate);
}

//! Surface defaults set only the flags the format defines.
NOLINT_TEST_F(GeometryStructureTest, RefusesUnknownSurfaceFlags)
{
  EXPECT_EQ(GeometryRefusal(WithSurfaceDefaults(
              [](SurfaceDesc& surface) -> void { surface.flags |= 0x80U; })),
    CookedDefect::kInvalidValue);
}

//! The default LOD policy keeps LodPolicyRecord's rules.
NOLINT_TEST_F(GeometryStructureTest, RefusesAnInvalidDefaultLodPolicy)
{
  auto policy = LodPolicyRecord {
    .type = LodPolicyType::kDistance,
    .boundary_count = 2,
  };
  policy.thresholds[0] = 0.75F;
  policy.thresholds[1] = 0.5F;
  auto geometry = Geometry();
  DescriptorDefects::SetDefaultLodPolicy(geometry, policy);

  EXPECT_EQ(GeometryRefusal(geometry), CookedDefect::kInvalidValue);
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Content/Loaders/GeometryLoader.h

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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
using oxygen::data::pak::geometry::MeshViewDesc;

constexpr auto kLast = std::numeric_limits<uint32_t>::max();

//! A geometry draws views its descriptor checks before any buffer is bound.
class GeometryStructureTest : public testing::Test {
protected:
  //! A standard geometry drawing one view of four vertices and six indices.
  [[nodiscard]] static auto Geometry() -> std::vector<std::byte>
  {
    auto geometry = GeometryBuilder { "Quad" };
    const auto lod = geometry.StandardLod(oxygen::ResourceIndexT { 1U },
      oxygen::ResourceIndexT { 2U }, glm::vec3 { 0.0F }, glm::vec3 { 1.0F });
    geometry.Submesh(lod,
      oxygen::data::SurfaceId::FromStableIdentity("GeometryStructure/surface"),
      {},
      {
        MeshViewDesc {
          .first_index = 0,
          .index_count = 6,
          .first_vertex = 0,
          .vertex_count = 4,
        },
      });
    return geometry.Bytes();
  }

  //! `Geometry()` with its view changed by `edit`.
  template <typename Edit>
  [[nodiscard]] static auto WithView(const Edit& edit) -> std::vector<std::byte>
  {
    auto geometry = Geometry();
    auto view = DescriptorDefects::MeshViewAt(geometry, 0);
    edit(view);
    DescriptorDefects::SetMeshView(geometry, 0, view);
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

} // namespace

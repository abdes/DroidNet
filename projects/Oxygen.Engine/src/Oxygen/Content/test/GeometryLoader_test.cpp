//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Fixtures/LoaderTestFixtures.h"

#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::loaders::LoadGeometryAsset;

namespace {

using oxygen::data::pak::geometry::GeometryAssetDesc;
using oxygen::data::pak::geometry::kGeometryAssetVersion;
using oxygen::data::pak::geometry::kSurfaceFlag_CastsShadows;
using oxygen::data::pak::geometry::kSurfaceFlag_Visible;
using oxygen::data::pak::geometry::LodPolicyRecord;
using oxygen::data::pak::geometry::LodPolicyType;
using oxygen::data::pak::geometry::MeshDesc;
using oxygen::data::pak::geometry::MeshViewDesc;
using oxygen::data::pak::geometry::SubMeshDesc;
using oxygen::data::pak::geometry::SurfaceDesc;

//! The procedural cube's vertex and index counts.
constexpr uint32_t kCubeVertexCount = 24U;
constexpr uint32_t kCubeIndexCount = 36U;
//! A mesh type value no MeshType names.
constexpr uint8_t kInvalidMeshType = 0xFFU;

//! The one surface the procedural test geometry declares.
const auto kSlot
  = oxygen::data::SurfaceId::FromStableIdentity("GeometryLoaderTest/slot");

//! Descriptor fields a test can make invalid.
struct GeometryOverrides {
  decltype(oxygen::data::pak::core::AssetHeader::version) version
    = kGeometryAssetVersion;
  //! Maximum x of the mesh view; the submesh bounds are all zero.
  float view_max_x = 0.0F;
  //! The surface table; the declared surface, drawn and casting, by default.
  std::vector<SurfaceDesc> surfaces {
    { .surface_id = kSlot,
      .flags = kSurfaceFlag_Visible | kSurfaceFlag_CastsShadows },
  };
  LodPolicyRecord lod_policy {};
};

class GeometryLoaderContractTest
  : public oxygen::content::testing::BinaryAssetLoaderFixtureBase {
protected:
  template <typename T> auto WriteBlob(const T& value) -> void
  {
    const auto bytes = std::as_bytes(std::span<const T, 1>(&value, 1));
    const auto result = desc_writer_.WriteBlob(bytes);
    ASSERT_TRUE(result) << result.error().message();
  }

  auto WriteProceduralGeometry(const std::string_view recipe_name,
    const std::span<const std::byte> parameters = {},
    const GeometryOverrides& overrides = {}) -> void
  {
    GeometryAssetDesc desc {};
    desc.header.asset_type
      = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
    desc.header.version = overrides.version;
    desc.lod_count = 1;
    desc.surface_count = static_cast<uint32_t>(overrides.surfaces.size());
    desc.default_lod_policy = overrides.lod_policy;
    auto strings = oxygen::data::DescriptorStringsBuilder {};
    MeshDesc mesh {};
    mesh.name_offset = strings.Add(recipe_name);
    mesh.mesh_type = static_cast<uint8_t>(oxygen::data::MeshType::kProcedural);
    mesh.submesh_count = 1;
    mesh.mesh_view_count = 1;
    mesh.info.procedural.params_size = static_cast<uint32_t>(parameters.size());
    SubMeshDesc submesh {};
    submesh.surface_id = kSlot;
    submesh.material_asset_key
      = oxygen::data::AssetKey::FromVirtualPath("/Art/Materials/Shared.omat");
    submesh.mesh_view_count = 1;
    MeshViewDesc view {
      .first_index = 0,
      .index_count = kCubeIndexCount,
      .first_vertex = 0,
      .vertex_count = kCubeVertexCount,
    };
    view.bounding_box_max[0] = overrides.view_max_x;
    const auto surface_bytes
      = std::as_bytes(std::span<const SurfaceDesc>(overrides.surfaces));
    desc.header.strings = {
      .offset = static_cast<oxygen::data::pak::core::StringTableOffsetT>(
        sizeof(desc) + sizeof(mesh) + parameters.size() + sizeof(submesh)
        + sizeof(view) + surface_bytes.size()),
      .size = strings.Size(),
    };
    auto packed = desc_writer_.ScopedAlignment(1);
    WriteBlob(desc);
    WriteBlob(mesh);
    if (!parameters.empty()) {
      ASSERT_TRUE(desc_writer_.WriteBlob(parameters));
    }
    WriteBlob(submesh);
    WriteBlob(view);
    if (!surface_bytes.empty()) {
      ASSERT_TRUE(desc_writer_.WriteBlob(surface_bytes));
    }
    ASSERT_TRUE(desc_writer_.WriteBlob(strings.Bytes()));
  }

  //! Loads the written geometry; returns the loader's error, else empty.
  auto LoadError() -> std::string
  {
    auto [context, collector] = MakeDecodeLoaderContext();
    try {
      (void)LoadGeometryAsset(context);
    } catch (const std::exception& error) {
      return error.what();
    }
    return {};
  }
};

//! Malformed mesh type is a structural decode error and must throw.
NOLINT_TEST_F(
  GeometryLoaderContractTest, LoadGeometryAssetUnsupportedMeshTypeThrows)
{
  GeometryAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
  desc.header.version = kGeometryAssetVersion;
  desc.lod_count = 1;

  MeshDesc mesh {};
  mesh.mesh_type = kInvalidMeshType;
  const auto strings = oxygen::data::DescriptorStringsBuilder {};
  desc.header.strings = {
    .offset = sizeof(desc) + sizeof(mesh),
    .size = strings.Size(),
  };

  {
    auto packed = desc_writer_.ScopedAlignment(1);
    WriteBlob(desc);
    WriteBlob(mesh);
    ASSERT_TRUE(desc_writer_.WriteBlob(strings.Bytes()));
  }

  const auto context = MakeLoaderContext(false, false);
  EXPECT_THROW({ (void)LoadGeometryAsset(context); }, std::runtime_error);
}

//! Unsupported recipes are decode errors, including metadata-only inspection.
NOLINT_TEST_F(GeometryLoaderContractTest, UnsupportedProceduralGeneratorThrows)
{
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("UnsupportedGenerator/Mesh"));
  for (const auto parse_only : { false, true }) {
    auto [context, collector] = MakeDecodeLoaderContext();
    context.parse_only = parse_only;
    try {
      (void)LoadGeometryAsset(context);
      FAIL() << "Unsupported procedural generator must fail decoding";
    } catch (const std::runtime_error& error) {
      EXPECT_NE(std::string(error.what()).find("UnsupportedGenerator/Mesh"),
        std::string::npos);
    }
  }
}

//! Rejected procedural dimensions must never reach Mesh's bounds calculation.
NOLINT_TEST_F(GeometryLoaderContractTest, InvalidProceduralParametersThrow)
{
  // Capsule requires at least one hemisphere segment; a complete zero-valued
  // first parameter is an invalid recipe, not a truncated descriptor.
  const uint32_t hemisphere_segments = 0;
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry(
    "Capsule/Mesh", std::as_bytes(std::span(&hemisphere_segments, 1))));
  for (const auto parse_only : { false, true }) {
    auto [context, collector] = MakeDecodeLoaderContext();
    context.parse_only = parse_only;
    EXPECT_THROW((void)LoadGeometryAsset(context), std::runtime_error);
  }
}

//! Valid procedural recipes still materialize in parse-only inspector mode.
NOLINT_TEST_F(
  GeometryLoaderContractTest, ValidProceduralMeshLoadsInDecodeAndParseOnlyModes)
{
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("Cube/Mesh"));
  for (const auto parse_only : { false, true }) {
    auto [context, collector] = MakeDecodeLoaderContext();
    context.parse_only = parse_only;
    const auto geometry = LoadGeometryAsset(context);
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->LodCount(), 1U);
    const auto& mesh = geometry->MeshAt(oxygen::data::LodIndex {});
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->VertexCount(), kCubeVertexCount);
    EXPECT_EQ(mesh->IndexCount(), kCubeIndexCount);
    ASSERT_EQ(collector->AssetDependencies().size(), 1U);
  }
}

//! Geometry cooked before mesh views carried bounds must be re-cooked.
NOLINT_TEST_F(GeometryLoaderContractTest, PreviousGeometryVersionIsRejected)
{
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("Cube/Mesh", {},
    {
      .version = kGeometryAssetVersion - 1U,
    }));
  auto [context, collector] = MakeDecodeLoaderContext();
  try {
    (void)LoadGeometryAsset(context);
    FAIL() << "A previous geometry version must not load";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("recooking"), std::string::npos);
  }
}

//! Mesh view bounds must lie inside their submesh bounds.
NOLINT_TEST_F(GeometryLoaderContractTest, ViewBoundsOutsideSubmeshAreRejected)
{
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("Cube/Mesh", {},
    {
      .view_max_x = 1.0F,
    }));
  auto [context, collector] = MakeDecodeLoaderContext();
  try {
    (void)LoadGeometryAsset(context);
    FAIL() << "Mesh view bounds outside the submesh must not load";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("outside the submesh bounds"),
      std::string::npos);
  }
}

//! Dependency inspection reads material keys without external buffer readers.
NOLINT_TEST_F(
  GeometryLoaderContractTest, InspectDependenciesWithoutLoadingBuffers)
{
  const auto material
    = oxygen::data::AssetKey::FromVirtualPath("/Art/Materials/Shared.omat");
  GeometryAssetDesc desc {};
  desc.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kGeometry);
  desc.header.version = kGeometryAssetVersion;
  desc.lod_count = 1;
  desc.surface_count = 1;
  MeshDesc mesh {};
  mesh.mesh_type = static_cast<uint8_t>(oxygen::data::MeshType::kStandard);
  mesh.submesh_count = 2;
  mesh.mesh_view_count = 2;
  SubMeshDesc submesh {};
  submesh.material_asset_key = material;
  submesh.surface_id = kSlot;
  submesh.mesh_view_count = 1;
  const MeshViewDesc view {
    .first_index = 0,
    .index_count = 3,
    .first_vertex = 0,
    .vertex_count = 3,
  };
  const SurfaceDesc surface {
    .surface_id = kSlot,
    .flags = kSurfaceFlag_Visible,
  };
  const auto strings = oxygen::data::DescriptorStringsBuilder {};
  desc.header.strings = {
    .offset = sizeof(desc) + sizeof(mesh)
      + (2U * (sizeof(submesh) + sizeof(view))) + sizeof(surface),
    .size = strings.Size(),
  };
  {
    auto packed = desc_writer_.ScopedAlignment(1);
    WriteBlob(desc);
    WriteBlob(mesh);
    WriteBlob(submesh);
    WriteBlob(view);
    WriteBlob(submesh);
    WriteBlob(view);
    WriteBlob(surface);
    ASSERT_TRUE(desc_writer_.WriteBlob(strings.Bytes()));
  }
  auto context = MakeLoaderContext(true, true);
  const auto collector
    = std::make_shared<oxygen::content::internal::DependencyCollector>();
  context.dependency_collector = collector;
  const auto geometry = oxygen::content::loaders::LoadGeometryAsset(context);
  ASSERT_NE(geometry, nullptr);
  EXPECT_THAT(collector->AssetDependencies(), ::testing::ElementsAre(material));
}

//! The surface table and the default LOD policy reach the geometry asset.
NOLINT_TEST_F(GeometryLoaderContractTest, AssetDefaultsLoad)
{
  auto policy = LodPolicyRecord {
    .type = LodPolicyType::kDistance,
    .boundary_count = 2,
    .hysteresis_ratio = 0.25F,
  };
  policy.thresholds[0] = 0.5F;
  policy.thresholds[1] = 0.75F;
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("Cube/Mesh", {},
    {
      .surfaces
      = { { .surface_id = kSlot, .flags = kSurfaceFlag_CastsShadows } },
      .lod_policy = policy,
    }));
  auto [context, collector] = MakeDecodeLoaderContext();
  const auto geometry = LoadGeometryAsset(context);

  const auto* surface = geometry->FindSurface(kSlot);
  ASSERT_NE(surface, nullptr);
  ASSERT_TRUE(surface->defaults.has_value());
  EXPECT_FALSE(surface->defaults->visible);
  EXPECT_TRUE(surface->defaults->casts_shadows);
  ASSERT_TRUE(geometry->DefaultLodPolicy().has_value());
  EXPECT_EQ(geometry->DefaultLodPolicy()->type, LodPolicyType::kDistance);
  EXPECT_EQ(geometry->DefaultLodPolicy()->boundary_count, 2U);
  EXPECT_EQ(geometry->DefaultLodPolicy()->thresholds[1], 0.75F);
}

//! A cooked geometry must declare the defaults of every surface it declares.
NOLINT_TEST_F(GeometryLoaderContractTest, MissingSurfaceTableIsRejected)
{
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("Cube/Mesh", {},
    {
      .surfaces = {},
    }));
  EXPECT_THAT(LoadError(), ::testing::HasSubstr("declared surfaces"));
}

//! The surface table lists the declared surfaces, and no other.
NOLINT_TEST_F(GeometryLoaderContractTest, UndeclaredSurfaceDefaultsAreRejected)
{
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("Cube/Mesh", {},
    {
      .surfaces = { {
        .surface_id = oxygen::data::SurfaceId::FromStableIdentity(
          "GeometryLoaderTest/other"),
        .flags = kSurfaceFlag_Visible,
      } },
    }));
  EXPECT_THAT(LoadError(), ::testing::HasSubstr("declared surfaces"));
}

//! A default LOD policy breaking LodPolicyRecord's rules does not load.
NOLINT_TEST_F(GeometryLoaderContractTest, InvalidDefaultLodPolicyIsRejected)
{
  auto policy = LodPolicyRecord {
    .type = LodPolicyType::kDistance,
    .boundary_count = 2,
  };
  policy.thresholds[0] = 0.75F;
  policy.thresholds[1] = 0.5F;
  ASSERT_NO_FATAL_FAILURE(WriteProceduralGeometry("Cube/Mesh", {},
    {
      .lod_policy = policy,
    }));
  EXPECT_THAT(LoadError(), ::testing::HasSubstr("LOD policy"));
}

} // namespace

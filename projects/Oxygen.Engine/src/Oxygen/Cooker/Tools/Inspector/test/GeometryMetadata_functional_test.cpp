//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/GeometryMetadata.cpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/GeometryMetadata.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/GeometryBuilder.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace geometry = oxygen::data::pak::geometry;
using oxygen::content::inspection::GeometryMetadataOptions;

//! `RunGeometryMetadataReport` as the tool runs it: the error, if any, goes to
//! stderr, and the result is the process exit code.
auto RunGeometryMetadataReport(const GeometryMetadataOptions& options) -> int
{
  return oxygen::content::toolkit::ExitCodeOf(std::cerr, [&] {
    return oxygen::content::inspection::RunGeometryMetadataReport(options);
  });
}

//! Throws when the loose cooked root `root` is not one the runtime loads.
auto ValidateRoot(const std::filesystem::path& root) -> void
{
  using oxygen::content::cooked::CookedContent;
  using oxygen::content::cooked::RealCookerStorage;
  using oxygen::content::cooked::Validate;
  Validate(CookedContent::Open(RealCookerStorage(), root));
}

using oxygen::cooker::testing::LooseAsset;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::data::AssetKey;
using oxygen::data::SurfaceId;

constexpr auto kGeometryPath = "/Content/Geometry/Inspection.ogeo";
constexpr auto kSharedMaterialPath = "/Content/Materials/Shared.omat";
constexpr uint32_t kTriangleSize = 3U;

auto MakeSurfaceId(const uint8_t value) -> SurfaceId
{
  SurfaceId::ByteArray bytes {};
  bytes.back() = value;
  return SurfaceId::FromBytes(bytes);
}

struct GeometryFixture {
  uint8_t version = geometry::kGeometryAssetVersion;
  SurfaceId first_surface = MakeSurfaceId(1);
};

//! Adds the buffers shared by every LOD: one triangle's vertices and indices,
//! at table entries 1 and 2 (entry 0 is the reserved fallback).
auto AddTriangleBuffers(LooseCookedRoot& library) -> void
{
  const auto vertex_data = std::array<oxygen::data::Vertex, kTriangleSize> {};
  const auto index_data = std::array<uint32_t, kTriangleSize> { 0U, 1U, 2U };
  library.AddBuffer(std::as_bytes(std::span(vertex_data)),
    {
      .usage_flags = 0x01U,
      .element_stride = sizeof(oxygen::data::Vertex),
    });
  library.AddBuffer(std::as_bytes(std::span(index_data)),
    {
      .usage_flags = 0x02U,
      .element_format = static_cast<uint8_t>(oxygen::Format::kR32UInt),
    });
}

//! The `Content` library holding the triangle buffers.
auto ContentRoot() -> LooseCookedRoot
{
  auto library = LooseCookedRoot { "Content" };
  AddTriangleBuffers(library);
  return library;
}

//! The geometry of the inspection fixture: two LODs of the standard triangle
//! mesh, each drawing `first_surface` and the second surface, which casts no
//! shadows; the first surface has no material in the second LOD.
auto FixtureGeometry(const SurfaceId& first_surface)
  -> oxygen::cooker::testing::GeometryBuilder
{
  using oxygen::cooker::testing::GeometryBuilder;
  const auto material = AssetKey::FromVirtualPath(kSharedMaterialPath);
  const auto view = geometry::MeshViewDesc {
    .first_index = 0,
    .index_count = kTriangleSize,
    .first_vertex = 0,
    .vertex_count = kTriangleSize,
  };
  auto builder = GeometryBuilder { "Inspection" };
  for (uint32_t lod = 0; lod < 2U; ++lod) {
    // The triangle's vertices all lie at the origin.
    const auto index = builder.StandardLod(oxygen::ResourceIndexT { 1U },
      oxygen::ResourceIndexT { 2U }, glm::vec3 { 0.0F }, glm::vec3 { 0.0F });
    builder.Submesh(
      index, first_surface, lod == 1U ? AssetKey {} : material, { view });
    builder.Submesh(index, MakeSurfaceId(2), material, { view });
  }
  builder.SurfaceFlags(MakeSurfaceId(2), geometry::kSurfaceFlag_Visible);
  builder.DefaultLodPolicy({
    .type = geometry::LodPolicyType::kFixed,
    .lod_index = 1,
  });
  return builder;
}

//! The geometry asset. The retired version and the nil surface are defects
//! applied to the valid descriptor: the product loader would not let the
//! builder write them.
auto GeometryAsset(const GeometryFixture& fixture) -> LooseAsset
{
  using oxygen::cooker::testing::DescriptorDefects;
  // A nil surface is built as a valid one, then cleared.
  const auto nil_surface = fixture.first_surface.IsNil();
  const auto first_surface = nil_surface
    ? SurfaceId::FromStableIdentity("cleared-surface")
    : fixture.first_surface;
  const auto builder = FixtureGeometry(first_surface);
  auto asset = LooseAsset {
    .type = oxygen::data::AssetType::kGeometry,
    .leaf = "Geometry/Inspection.ogeo",
    .descriptor = builder.Bytes(),
    .references = builder.References(),
  };
  if (nil_surface) {
    DescriptorDefects::ClearSurface(asset.descriptor, first_surface);
  }
  if (fixture.version != geometry::kGeometryAssetVersion) {
    DescriptorDefects::SetVersion(asset.descriptor, fixture.version);
  }
  return asset;
}

auto WriteGeometryRoot(const std::filesystem::path& root,
  const GeometryFixture& fixture = {}) -> void
{
  auto library = ContentRoot();
  library.Add(GeometryAsset(fixture));
  Materialize(library.Files(), root);
}

auto ExpectedLayoutRevision() -> oxygen::base::Sha256Digest
{
  const auto material = AssetKey::FromVirtualPath(kSharedMaterialPath);
  const auto surfaces = std::array {
    oxygen::data::Surface {
      .surface_id = MakeSurfaceId(1),
      .display_name = "Surface",
      .bindings = {
        { .lod_index = oxygen::data::LodIndex {}, .submesh_index = oxygen::data::SubmeshIndex {}, .default_material_key = material },
        { .lod_index = oxygen::data::LodIndex { 1U }, .submesh_index = oxygen::data::SubmeshIndex {}, .default_material_key = {} },
      },
      .defaults = std::nullopt,
    },
    oxygen::data::Surface {
      .surface_id = MakeSurfaceId(2),
      .display_name = "Surface",
      .bindings = {
        { .lod_index = oxygen::data::LodIndex {}, .submesh_index = oxygen::data::SubmeshIndex { 1U }, .default_material_key = material },
        { .lod_index = oxygen::data::LodIndex { 1U }, .submesh_index = oxygen::data::SubmeshIndex { 1U }, .default_material_key = material },
      },
      .defaults = std::nullopt,
    },
  };
  return oxygen::data::ComputeSurfaceLayoutRevision(surfaces).value();
}

//! Rewrites the root with the geometry and a scene overriding one of its
//! surfaces on the scene's only renderable node.
auto WriteOverrideScene(const std::filesystem::path& root,
  const oxygen::data::SurfaceId surface,
  const oxygen::base::Sha256Digest& layout_revision) -> void
{
  auto value = oxygen::data::pak::world::OverrideValue {};
  value.as_asset
    = AssetKey::FromVirtualPath("/Content/Materials/Override.omat");
  auto scene = SceneBuilder { "Inspection" };
  const auto node = scene.Node("Root");
  scene.Renderable(node, AssetKey::FromVirtualPath(kGeometryPath));
  scene.Override(node,
    oxygen::data::schema::OverrideId::kGeometrySurfaceMaterial, value, surface,
    layout_revision);
  auto library = ContentRoot();
  library.Add(GeometryAsset({}));
  library.Scene("Scenes/Inspection.oscene", scene);
  Materialize(library.Files(), root);
}

class InspectorGeometryMetadataTest : public oxygen::testing::TempDirTest {
protected:
  //! A report beside the cooked root, which is `TempDir()`.
  [[nodiscard]] auto ReportPath(const std::string_view name) const
    -> std::filesystem::path
  {
    return reports_.Path() / name;
  }

  auto Report() -> int
  {
    return RunGeometryMetadataReport({
      .cooked_root = TempDir().string(),
      .output = ReportPath("geometries.json").string(),
      .virtual_path = {},
    });
  }

  auto ReadReport() -> nlohmann::json
  {
    std::ifstream input(ReportPath("geometries.json"));
    auto report = nlohmann::json::parse(input);
    static const auto schema = oxygen::cooker::test::LoadSchema(
      "Tools/Inspector/Schemas/oxygen.cooked-geometries.schema.json");
    EXPECT_THAT(
      oxygen::cooker::test::ValidateJson(schema, report), ::testing::IsEmpty());
    return report;
  }

private:
  oxygen::testing::ScopedTempDir reports_;
};

NOLINT_TEST_F(InspectorGeometryMetadataTest, ReportsSeparateSurfacesAcrossLods)
{
  WriteGeometryRoot(TempDir());
  EXPECT_NO_THROW(ValidateRoot(TempDir()));
  ASSERT_EQ(Report(), 0);
  const auto report = ReadReport();
  ASSERT_EQ(report.at("geometries").size(), 1U);
  const auto& row = report.at("geometries").front();
  EXPECT_EQ(row.at("geometry_asset_key"),
    oxygen::data::to_string(
      AssetKey::FromVirtualPath("/Content/Geometry/Inspection.ogeo")));
  EXPECT_NE(row.at("layout_revision"), std::string(64, '0'));
  EXPECT_EQ(row.at("lod_policy"),
    nlohmann::json({ { "type", "fixed" }, { "lod_index", 1 } }));
  const auto& surfaces = row.at("surfaces");
  ASSERT_EQ(surfaces.size(), 2U);
  for (size_t surface_index = 0; surface_index < surfaces.size();
    ++surface_index) {
    const auto& surface = surfaces.at(surface_index);
    EXPECT_EQ(surface.at("surface_id"),
      oxygen::data::to_string(
        MakeSurfaceId(static_cast<uint8_t>(surface_index + 1))));
    EXPECT_EQ(surface.at("display_name"), "Surface");
    EXPECT_EQ(surface.at("visible"), true);
    EXPECT_EQ(surface.at("casts_shadows"), surface_index == 0);
    ASSERT_EQ(surface.at("bindings").size(), 2U);
    for (size_t lod = 0; lod < 2; ++lod) {
      const auto& binding = surface.at("bindings").at(lod);
      EXPECT_EQ(binding.at("lod_index"), lod);
      EXPECT_EQ(binding.at("submesh_index"), surface_index);
      const auto material = lod == 1 && surface_index == 0
        ? AssetKey {}
        : AssetKey::FromVirtualPath("/Content/Materials/Shared.omat");
      EXPECT_EQ(
        binding.at("default_material_key"), oxygen::data::to_string(material));
      // Each submesh draws one view of one triangle.
      EXPECT_EQ(binding.at("triangle_count"), kTriangleSize / 3U);
    }
  }
  ASSERT_EQ(Report(), 0);
  EXPECT_EQ(ReadReport(), report);
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsRetiredGeometryVersion)
{
  WriteGeometryRoot(TempDir(), { .version = 1 });
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
  EXPECT_EQ(Report(), 2);
  EXPECT_FALSE(std::filesystem::exists(ReportPath("geometries.json")));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, FiltersByNativeVirtualPath)
{
  WriteGeometryRoot(TempDir());
  EXPECT_EQ(RunGeometryMetadataReport({
              .cooked_root = TempDir().string(),
              .output = ReportPath("geometries.json").string(),
              .virtual_path = "/Content/Geometry/Absent.ogeo",
            }),
    0);
  EXPECT_TRUE(ReadReport().at("geometries").empty());
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsNilSurfaceIdentity)
{
  WriteGeometryRoot(TempDir(), { .first_surface = {} });
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
  EXPECT_EQ(Report(), 2);
  EXPECT_FALSE(std::filesystem::exists(ReportPath("geometries.json")));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, DoesNotOverwriteSourceDescriptor)
{
  WriteGeometryRoot(TempDir());
  EXPECT_EQ(
    RunGeometryMetadataReport({
      .cooked_root = TempDir().string(),
      .output = TempPath("Descriptors/Geometry/Inspection.ogeo").string(),
      .virtual_path = {},
    }),
    1);
  EXPECT_NO_THROW(ValidateRoot(TempDir()));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, AcceptsLocalSurfaceOverride)
{
  WriteGeometryRoot(TempDir());
  WriteOverrideScene(TempDir(), MakeSurfaceId(2), ExpectedLayoutRevision());
  EXPECT_NO_THROW(ValidateRoot(TempDir()));
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsUnknownLocalSurfaceOverride)
{
  WriteGeometryRoot(TempDir());
  WriteOverrideScene(TempDir(), MakeSurfaceId(3), ExpectedLayoutRevision());
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
}

NOLINT_TEST_F(InspectorGeometryMetadataTest, RejectsStaleLocalSurfaceLayout)
{
  WriteGeometryRoot(TempDir());
  auto revision = ExpectedLayoutRevision();
  revision.front() ^= 1U;
  WriteOverrideScene(TempDir(), MakeSurfaceId(2), revision);
  EXPECT_THROW(ValidateRoot(TempDir()), std::runtime_error);
}

//! A pak reports the same surface inventory as the loose root it was packed
//! from.
NOLINT_TEST_F(InspectorGeometryMetadataTest, ReportsSurfacesFromAPak)
{
  // Arrange
  const auto loose = TempPath("loose");
  auto library = ContentRoot();
  library.Add(GeometryAsset({}));
  library.Material("Materials/Shared.omat",
    oxygen::cooker::testing::MaterialBuilder { "Shared" });
  Materialize(library.Files(), loose);
  auto workspace = PakWorkspace {};
  workspace.PackOrThrow(workspace.FullRequest({ workspace.Add(library) }));
  const auto pak = TempPath("content.pak");
  Materialize(workspace.Storage().Bytes(PakWorkspace::PakPath("out.pak")), pak);
  ASSERT_EQ(RunGeometryMetadataReport({
              .cooked_root = loose.string(),
              .output = ReportPath("loose.json").string(),
              .virtual_path = {},
            }),
    0);

  // Act
  const auto exit_code = RunGeometryMetadataReport({
    .cooked_root = pak.string(),
    .output = ReportPath("geometries.json").string(),
    .virtual_path = "/Content/Geometry/Inspection.ogeo",
  });

  // Assert
  ASSERT_EQ(exit_code, 0);
  std::ifstream expected(ReportPath("loose.json"));
  EXPECT_EQ(ReadReport(), nlohmann::json::parse(expected));
}

} // namespace

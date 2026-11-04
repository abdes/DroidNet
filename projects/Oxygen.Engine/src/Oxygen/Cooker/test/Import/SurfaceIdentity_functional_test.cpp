//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/SurfaceAllocation.cpp,
//   Import/Internal/fbx/FbxAdapter.cpp, Import/Internal/gltf/GltfAdapter.cpp

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/ext/vector_float3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/ModelImportTestBase.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/RegionStream.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::CoordinateConversionPolicy;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::SurfaceProvenance;
using oxygen::content::import::UnitNormalizationPolicy;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::data::SurfaceId;
namespace import_codes = oxygen::content::import::import_codes;

struct SourceCapture {
  std::shared_ptr<const SurfaceProvenance> provenance;
  glm::vec3 bounds_max {};
};

class SurfaceIdentityTest
  : public oxygen::content::import::test::ModelImportTestBase {
protected:
  static auto FixturePath(const std::string& extension) -> std::filesystem::path
  {
    return oxygen::cooker::test::ModelPath(
      "static_scalar_triangle." + extension);
  }

  [[nodiscard]] auto ChangedSourcePath(const std::string& extension) const
    -> std::filesystem::path
  {
    return MakeTempDir("surface_identity_edit_" + TestName())
      / ("static_scalar_triangle." + extension);
  }

  [[nodiscard]] auto Request(const std::filesystem::path& source)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = source;
    request.asset_virtual_path
      = oxygen::cooker::test::ModelVirtualPath(request.source_path);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = MakeTempDir("surface_identity_import_" + TestName()
      + "_" + std::to_string(capture_index_++));
    return request;
  }

  //! The glTF fixture with its only primitive drawn as points.
  [[nodiscard]] auto WritePointsSource() const -> std::filesystem::path
  {
    std::ifstream input(FixturePath("gltf"));
    auto source = nlohmann::json::parse(input);
    constexpr auto kPointsMode = 0;
    source.at("meshes")
      .at(0)
      .at("primitives")
      .at(0)
      .update({
        { "mode", kPointsMode },
      });
    const auto path = ChangedSourcePath("gltf");
    std::ofstream output(path);
    output << source.dump();
    if (!output.good()) {
      throw std::runtime_error("Failed to write the points glTF fixture");
    }
    return path;
  }

  //! The fixture with one primitive per name of `materials`, each a copy of
  //! its only one, bound to that material.
  [[nodiscard]] auto WriteMaterialsSource(
    const std::vector<std::string>& materials) const -> std::filesystem::path
  {
    std::ifstream input(FixturePath("gltf"));
    auto source = nlohmann::json::parse(input);
    const auto material = source.at("materials").at(0);
    const auto primitive = source.at("meshes").at(0).at("primitives").at(0);
    auto written_materials = nlohmann::json::array();
    auto primitives = nlohmann::json::array();
    for (const auto& name : materials) {
      auto written = material;
      written["name"] = name;
      auto bound = primitive;
      bound["material"] = written_materials.size();
      written_materials.push_back(std::move(written));
      primitives.push_back(std::move(bound));
    }
    source["materials"] = std::move(written_materials);
    source["meshes"][0]["primitives"] = std::move(primitives);
    const auto path = ChangedSourcePath("gltf");
    std::ofstream output(path);
    output << source.dump();
    if (!output.good()) {
      throw std::runtime_error("Failed to write the materials glTF fixture");
    }
    return path;
  }

  [[nodiscard]] static auto FbxFixtureText() -> std::string
  {
    std::ifstream input(FixturePath("fbx"));
    return { std::istreambuf_iterator<char> { input },
      std::istreambuf_iterator<char> {} };
  }

  [[nodiscard]] auto WriteFbxText(const std::string& text) const
    -> std::filesystem::path
  {
    const auto path = ChangedSourcePath("fbx");
    std::ofstream output(path);
    output << text;
    if (!output.good()) {
      throw std::runtime_error("Failed to write the FBX fixture");
    }
    return path;
  }

  //! The FBX fixture with `after` replacing `before`.
  [[nodiscard]] auto WriteFbxSource(const std::string_view before,
    const std::string_view after) const -> std::filesystem::path
  {
    auto text = FbxFixtureText();
    const auto offset = text.find(before);
    if (offset == std::string::npos) {
      throw std::runtime_error("The FBX fixture changed");
    }
    text.replace(offset, before.size(), after);
    return WriteFbxText(text);
  }

  //! The FBX fixture with its mesh bound to a material named `name`.
  [[nodiscard]] auto WriteFbxMaterialSource(const std::string& name) const
    -> std::filesystem::path
  {
    auto text = FbxFixtureText();
    constexpr auto kModelToRoot = std::string_view { "C: \"OO\",1002,0" };
    const auto connection = text.find(kModelToRoot);
    if (connection == std::string::npos) {
      throw std::runtime_error("The FBX fixture changed");
    }
    text.insert(connection, "C: \"OO\",1003,1002\n    ");
    const auto objects_end = text.rfind('}', text.find("Connections:"));
    text.insert(objects_end,
      "    Material: 1003, \"Material::" + name
        + "\", \"\" {\n        ShadingModel: \"phong\"\n    }\n");
    return WriteFbxText(text);
  }

  //! Imports `source` over the recorded provenance; returns what it records
  //! for the model's one geometry.
  auto Allocations(const std::filesystem::path& source)
    -> std::map<std::string, SurfaceId>
  {
    last_report_ = RunImport(Request(source)).report;
    EXPECT_TRUE(last_report_.success)
      << DiagnosticSummary(last_report_.diagnostics);
    if (source_provenance_->Geometries().size() != 1U) {
      ADD_FAILURE() << "The model has no one geometry";
      return {};
    }
    return source_provenance_->Geometries().front().allocations;
  }

  oxygen::content::import::ImportReport last_report_;

  auto Capture(const std::filesystem::path& source,
    const CoordinateConversionPolicy& coordinates) -> SourceCapture
  {
    auto request = ImportRequest {};
    request.source_path = source;
    request.asset_virtual_path
      = oxygen::cooker::test::ModelVirtualPath(request.source_path);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = MakeTempDir("surface_identity_cook_" + TestName()
      + "_" + std::to_string(capture_index_++));
    request.options.coordinate = coordinates;
    const auto imported = RunImport(std::move(request));
    EXPECT_TRUE(imported.report.success);
    if (!imported.report.success) {
      for (const auto& diagnostic : imported.report.diagnostics) {
        ADD_FAILURE() << diagnostic.code << ": " << diagnostic.message;
      }
      return {};
    }
    const auto inspection = LoadInspection(imported.report.cooked_root);
    const auto geometry_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kGeometry);
    EXPECT_TRUE(geometry_entry.has_value());
    if (!geometry_entry) {
      return {};
    }
    auto file = oxygen::serio::NativeFile::Open(
      inspection.DescriptorStored(geometry_entry->key).file, {});
    EXPECT_TRUE(file) << file.error().message();
    if (!file) {
      return {};
    }
    auto stream = oxygen::serio::OpenReadStream(*file);
    EXPECT_TRUE(stream) << stream.error().message();
    if (!stream) {
      return {};
    }
    oxygen::serio::Reader reader(*stream);
    const oxygen::content::LoaderContext context {
      .current_asset_key = geometry_entry->key,
      .desc_reader = &reader,
      .work_offline = true,
      .parse_only = true,
    };
    const auto geometry = oxygen::content::loaders::LoadGeometryAsset(context);
    return {
      .provenance = source_provenance_,
      .bounds_max = geometry->BoundingBoxMax(),
    };
  }

  auto VerifyProducerCoordinates(const std::string& extension) -> void
  {
    auto coordinates = CoordinateConversionPolicy {};
    coordinates.bake_transforms_into_meshes = false;
    const auto original = Capture(FixturePath(extension), coordinates);
    ASSERT_NE(original.provenance, nullptr);
    ASSERT_EQ(original.provenance->Geometries().size(), 1U);
    const auto& source = original.provenance->Geometries().front();
    for (const auto policy : {
           UnitNormalizationPolicy::kPreserveSource,
           UnitNormalizationPolicy::kApplyCustomFactor,
         }) {
      coordinates.unit_normalization = policy;
      coordinates.unit_scale = 10.0F;
      const auto converted = Capture(FixturePath(extension), coordinates);
      ASSERT_NE(converted.provenance, nullptr);
      ASSERT_EQ(converted.provenance->Geometries().size(), 1U);
      const auto& candidate = converted.provenance->Geometries().front();
      EXPECT_EQ(
        candidate.source_geometry_anchor, source.source_geometry_anchor);
      EXPECT_EQ(candidate.allocations, source.allocations);
      if (policy == UnitNormalizationPolicy::kApplyCustomFactor) {
        EXPECT_NE(converted.bounds_max, original.bounds_max);
      }
    }
    coordinates.bake_transforms_into_meshes = true;
    const auto baked = Capture(FixturePath(extension), coordinates);
    ASSERT_NE(baked.provenance, nullptr);
    ASSERT_FALSE(baked.provenance->Geometries().empty());
    for (const auto& variant : baked.provenance->Geometries()) {
      EXPECT_EQ(variant.allocations.size(), source.allocations.size());
    }
  }

private:
  static auto TestName() -> std::string
  {
    return testing::UnitTest::GetInstance()->current_test_info()->name();
  }

  size_t capture_index_ = 0;
};

NOLINT_TEST_F(SurfaceIdentityTest, GltfCoordinatesAndBakePolicyKeepSurfaceIds)
{
  VerifyProducerCoordinates("gltf");
}

NOLINT_TEST_F(SurfaceIdentityTest, FbxCoordinatesAndBakePolicyKeepSurfaceIds)
{
  VerifyProducerCoordinates("fbx");
}

//! An edit of the vertices or the polygons is not a new surface: everything
//! that holds on to an ID stays bound.
NOLINT_TEST_F(SurfaceIdentityTest, FbxVertexAndPolygonEditsKeepSurfaceIds)
{
  const auto before = Allocations(FixturePath("fbx"));
  for (const auto& [from, to] : {
         std::pair { "a: 0,0,0,1,0,0,0,1,0", "a: 0,0,0,2,0,0,0,1,0" },
         std::pair { "a: 0,1,-3", "a: 1,0,-3" },
       }) {
    SCOPED_TRACE(from);
    EXPECT_EQ(Allocations(WriteFbxSource(from, to)), before);
    EXPECT_FALSE(HasDiagnosticCode(
      last_report_.diagnostics, import_codes::kSurfacesChanged));
  }
}

//! The same in glTF, where a primitive that adds triangles under a material the
//! mesh already uses adds no surface.
NOLINT_TEST_F(
  SurfaceIdentityTest, GltfPrimitiveOfTheSameMaterialKeepsSurfaceIds)
{
  const auto before = Allocations(WriteMaterialsSource({ "Paint" }));
  std::ifstream input(ChangedSourcePath("gltf"));
  auto source = nlohmann::json::parse(input);
  auto primitive = source.at("meshes").at(0).at("primitives").at(0);
  source.at("meshes").at(0).at("primitives").push_back(std::move(primitive));
  const auto path = ChangedSourcePath("gltf");
  {
    std::ofstream output(path);
    output << source.dump();
    ASSERT_TRUE(output.good());
  }

  EXPECT_EQ(Allocations(path), before);
}

//! A slot added or reordered takes nothing from the others.
NOLINT_TEST_F(SurfaceIdentityTest, GltfAddedAndReorderedMaterialsKeepTheOthers)
{
  const auto before = Allocations(WriteMaterialsSource({ "Paint", "Trim" }));
  ASSERT_EQ(before.size(), 2U);

  const auto added
    = Allocations(WriteMaterialsSource({ "Glass", "Trim", "Paint" }));

  ASSERT_EQ(added.size(), 3U);
  EXPECT_EQ(added.at("Paint"), before.at("Paint"));
  EXPECT_EQ(added.at("Trim"), before.at("Trim"));
  EXPECT_TRUE(HasDiagnosticCode(
    last_report_.diagnostics, import_codes::kSurfacesChanged));
}

//! A slot that goes comes back with its ID when its name does.
NOLINT_TEST_F(SurfaceIdentityTest, GltfRemovedMaterialGetsItsIdBackWithItsName)
{
  const auto before = Allocations(WriteMaterialsSource({ "Paint", "Trim" }));
  const auto removed = Allocations(WriteMaterialsSource({ "Trim" }));
  ASSERT_EQ(removed.size(), 1U);
  EXPECT_EQ(removed.at("Trim"), before.at("Trim"));

  EXPECT_EQ(Allocations(WriteMaterialsSource({ "Paint", "Trim" })), before);
}

//! Renaming one material, as a DCC's cleanup does, keeps its ID in either
//! format.
NOLINT_TEST_F(SurfaceIdentityTest, RenamingOneMaterialKeepsItsId)
{
  {
    const auto before
      = Allocations(WriteMaterialsSource({ "Material.001", "Trim" }));
    const auto after = Allocations(WriteMaterialsSource({ "Paint", "Trim" }));
    ASSERT_EQ(after.size(), 2U);
    EXPECT_EQ(after.at("Paint"), before.at("Material.001"));
    EXPECT_EQ(after.at("Trim"), before.at("Trim"));
  }
  source_provenance_
    = std::make_shared<const SurfaceProvenance>(oxygen::Uuid::Generate());
  const auto before = Allocations(WriteFbxMaterialSource("Material.001"));
  ASSERT_TRUE(before.contains("Material.001"));
  const auto after = Allocations(WriteFbxMaterialSource("Paint"));
  ASSERT_TRUE(after.contains("Paint"));
  EXPECT_EQ(after.at("Paint"), before.at("Material.001"));
}

//! With two renamed at once nothing says which is which: both get new IDs.
NOLINT_TEST_F(SurfaceIdentityTest, GltfTwoMaterialsRenamedAtOnceGetNewIds)
{
  const auto before = Allocations(WriteMaterialsSource({ "Paint", "Trim" }));
  const auto after = Allocations(WriteMaterialsSource({ "Body", "Rim" }));
  ASSERT_EQ(after.size(), 2U);
  EXPECT_NE(after.at("Body"), before.at("Paint"));
  EXPECT_NE(after.at("Rim"), before.at("Trim"));
}

//! Two materials of one name stay separate surfaces, numbered in file order.
NOLINT_TEST_F(SurfaceIdentityTest, GltfMaterialsOfOneNameAreNumbered)
{
  const auto allocated
    = Allocations(WriteMaterialsSource({ "Paint", "Paint" }));
  ASSERT_EQ(allocated.size(), 2U);
  EXPECT_NE(allocated.at("Paint"), allocated.at("Paint[2]"));
}

//! Positions cgltf cannot read, as a VEC4 accessor's, drop the mesh rather than
//! importing it collapsed.
NOLINT_TEST_F(SurfaceIdentityTest, GltfUnreadablePositionsDropTheMesh)
{
  std::ifstream input(FixturePath("gltf"));
  auto source = nlohmann::json::parse(input);
  auto& primitive = source.at("meshes").at(0).at("primitives").at(0);
  const auto positions
    = primitive.at("attributes").at("POSITION").get<size_t>();
  primitive["attributes"] = { { "POSITION", positions } };
  primitive.erase("indices");
  auto& accessor = source.at("accessors").at(positions);
  accessor["type"] = "VEC4";
  accessor["count"] = 1;
  accessor.erase("min");
  accessor.erase("max");
  const auto path = ChangedSourcePath("gltf");
  {
    std::ofstream output(path);
    output << source.dump();
    ASSERT_TRUE(output.good());
  }

  const auto imported = RunImport(Request(path));

  EXPECT_TRUE(
    HasDiagnosticCode(imported.report.diagnostics, import_codes::kMeshDropped))
    << DiagnosticSummary(imported.report.diagnostics);
}

//! A primitive that is not a triangle list is dropped with a warning and the
//! model still imports; the published provenance lists only the geometry the
//! cook produced, so the emptied mesh has no entry.
NOLINT_TEST_F(SurfaceIdentityTest, GltfNonTrianglePrimitiveIsDropped)
{
  const auto original
    = Capture(FixturePath("gltf"), CoordinateConversionPolicy {});
  ASSERT_NE(original.provenance, nullptr);
  ASSERT_EQ(original.provenance->Geometries().size(), 1U);

  const auto dropped = RunImport(Request(WritePointsSource()));

  EXPECT_TRUE(dropped.report.success)
    << DiagnosticSummary(dropped.report.diagnostics);
  EXPECT_TRUE(HasDiagnosticCode(
    dropped.report.diagnostics, import_codes::kGltfPrimitiveType));
  ASSERT_NE(source_provenance_, nullptr);
  EXPECT_TRUE(source_provenance_->Geometries().empty());
}

//! Surface ids derive from the source, so a geometry an import dropped gets
//! its ids back once its source is repaired.
NOLINT_TEST_F(SurfaceIdentityTest, RepairedGeometryGetsItsSurfaceIdsBack)
{
  const auto original
    = Capture(FixturePath("gltf"), CoordinateConversionPolicy {});
  ASSERT_NE(original.provenance, nullptr);
  ASSERT_EQ(original.provenance->Geometries().size(), 1U);
  ASSERT_TRUE(RunImport(Request(WritePointsSource())).report.success);

  const auto repaired
    = Capture(FixturePath("gltf"), CoordinateConversionPolicy {});

  ASSERT_NE(repaired.provenance, nullptr);
  ASSERT_EQ(repaired.provenance->Geometries().size(), 1U);
  EXPECT_EQ(repaired.provenance->Geometries().front().allocations,
    original.provenance->Geometries().front().allocations);
}

//! A failed import publishes no provenance and leaves the recorded one as it
//! was.
NOLINT_TEST_F(SurfaceIdentityTest, FailedImportKeepsRecordedProvenance)
{
  const auto original
    = Capture(FixturePath("gltf"), CoordinateConversionPolicy {});
  ASSERT_NE(original.provenance, nullptr);
  const auto path = ChangedSourcePath("gltf");
  {
    std::ofstream output(path);
    output << "{ not glTF";
    ASSERT_TRUE(output.good());
  }

  const auto failed = RunImport(Request(path));

  EXPECT_FALSE(failed.report.success);
  EXPECT_TRUE(failed.report.surface_provenance_json.empty());
  EXPECT_EQ(source_provenance_->Serialize(), original.provenance->Serialize());
}

//! A geometry's surface ids are found by its name inside its model, so moving
//! or renaming the model keeps them; only the inventory's key follows the move.
NOLINT_TEST_F(SurfaceIdentityTest, MovingTheModelKeepsItsSurfaceIds)
{
  const auto original
    = Capture(FixturePath("gltf"), CoordinateConversionPolicy {});
  ASSERT_NE(original.provenance, nullptr);
  ASSERT_EQ(original.provenance->Geometries().size(), 1U);
  const auto before = original.provenance->Geometries().front();

  auto request = ImportRequest {};
  request.source_path = FixturePath("gltf");
  request.asset_virtual_path = "/Cooked/Moved/Renamed.gltf";
  request.source_mount_path
    = oxygen::cooker::test::SourceMountPath(request.source_path);
  request.cooked_root = MakeTempDir("surface_identity_moved");
  const auto moved = RunImport(std::move(request));
  ASSERT_TRUE(moved.report.success);
  ASSERT_EQ(source_provenance_->Geometries().size(), 1U);
  const auto& after = source_provenance_->Geometries().front();

  EXPECT_EQ(after.source_geometry_anchor, before.source_geometry_anchor);
  EXPECT_EQ(after.allocations, before.allocations);
  EXPECT_NE(
    after.inventory.geometry_asset_key, before.inventory.geometry_asset_key);
}

} // namespace

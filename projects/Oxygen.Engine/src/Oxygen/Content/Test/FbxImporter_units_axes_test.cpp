//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

#include "FbxImporterTest.h"

namespace {

using oxygen::content::LooseCookedInspection;
using oxygen::content::import::AssetImporter;
using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::UnitNormalizationPolicy;
using oxygen::data::AssetType;
using oxygen::data::pak::NodeRecord;
using oxygen::data::pak::SceneAssetDesc;
using oxygen::serio::FileStream;
using oxygen::serio::Reader;

class FbxImporterUnitsAxesTest : public oxygen::content::test::FbxImporterTest {
};

[[nodiscard]] auto EndsWith(std::string_view haystack, std::string_view suffix)
  -> bool
{
  if (suffix.size() > haystack.size()) {
    return false;
  }
  return haystack.substr(haystack.size() - suffix.size()) == suffix;
}

struct LoadedSceneDesc final {
  SceneAssetDesc header {};
  std::vector<NodeRecord> nodes;
  std::vector<char> strings;
};

[[nodiscard]] auto LoadSceneDescriptor(const std::filesystem::path& cooked_root)
  -> LoadedSceneDesc
{
  LooseCookedInspection inspection;
  inspection.LoadFromRoot(cooked_root);

  const auto assets = inspection.Assets();
  const auto scene_it = std::find_if(assets.begin(), assets.end(),
    [](const LooseCookedInspection::AssetEntry& e) {
      return e.asset_type == static_cast<uint8_t>(AssetType::kScene);
    });
  LoadedSceneDesc loaded;
  if (scene_it == assets.end()) {
    ADD_FAILURE() << "No Scene asset present in cooked output";
    return loaded;
  }

  const auto desc_path
    = cooked_root / std::filesystem::path(scene_it->descriptor_relpath);

  FileStream<> stream(desc_path, std::ios::in);
  Reader<FileStream<>> reader(stream);

  auto packed = reader.ScopedAlignment(1);

  const auto base_pos_res = reader.Position();
  if (!base_pos_res) {
    ADD_FAILURE() << "Failed to query descriptor reader position";
    return loaded;
  }
  const auto base_pos = *base_pos_res;

  auto header_result = reader.ReadBlobInto(
    std::as_writable_bytes(std::span<SceneAssetDesc, 1>(&loaded.header, 1)));
  if (!header_result) {
    ADD_FAILURE() << "Failed to read SceneAssetDesc header";
    return loaded;
  }

  if (loaded.header.nodes.count == 0u) {
    ADD_FAILURE() << "Scene descriptor has zero nodes";
    return loaded;
  }
  if (loaded.header.nodes.entry_size != sizeof(NodeRecord)) {
    ADD_FAILURE() << "Scene descriptor node entry_size mismatch";
    return loaded;
  }

  // Read node table.
  {
    auto seek_nodes
      = reader.Seek(base_pos + static_cast<size_t>(loaded.header.nodes.offset));
    if (!seek_nodes) {
      ADD_FAILURE() << "Failed to seek to node table";
      return loaded;
    }

    loaded.nodes.resize(loaded.header.nodes.count);
    auto nodes_result
      = reader.ReadBlobInto(std::as_writable_bytes(std::span(loaded.nodes)));
    if (!nodes_result) {
      ADD_FAILURE() << "Failed to read node table";
      return loaded;
    }
  }

  // Read scene string table.
  {
    auto seek_strings = reader.Seek(
      base_pos + static_cast<size_t>(loaded.header.scene_strings.offset));
    if (!seek_strings) {
      ADD_FAILURE() << "Failed to seek to string table";
      return loaded;
    }

    loaded.strings.resize(loaded.header.scene_strings.size);
    auto strings_result
      = reader.ReadBlobInto(std::as_writable_bytes(std::span(loaded.strings)));
    if (!strings_result) {
      ADD_FAILURE() << "Failed to read string table";
      return loaded;
    }

    if (loaded.strings.empty()) {
      ADD_FAILURE() << "String table is empty";
      return loaded;
    }
    EXPECT_EQ(loaded.strings[0], '\0');
  }

  return loaded;
}

[[nodiscard]] auto ReadStringAt(
  const std::vector<char>& table, const uint32_t offset) -> std::string_view
{
  if (offset >= table.size()) {
    return {};
  }

  const char* begin = table.data() + offset;
  const char* end
    = static_cast<const char*>(std::memchr(begin, '\0', table.size() - offset));
  if (end == nullptr) {
    return {};
  }

  return std::string_view(begin, static_cast<size_t>(end - begin));
}

[[nodiscard]] auto FindNodeByNameSuffix(const LoadedSceneDesc& scene,
  std::string_view suffix) -> std::optional<NodeRecord>
{
  for (const auto& node : scene.nodes) {
    const auto name = ReadStringAt(scene.strings, node.scene_name_offset);
    if (!name.empty() && EndsWith(name, suffix)) {
      return node;
    }
  }
  return std::nullopt;
}

//! Test: NormalizeToMeters scales node translations using FBX unit settings.
/*!
 Scenario: An ASCII FBX declares centimeter units (UnitScaleFactor=1). A node
 translation is authored as 100 units (100 cm).

 Verifies: With UnitNormalizationPolicy::kNormalizeToMeters, the emitted scene
 node translation is 1 meter.
*/
NOLINT_TEST_F(FbxImporterUnitsAxesTest, RealBackend_NormalizesUnitsToMeters)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_units_normalize_to_meters");
  const auto source_path = temp_dir / "units_cm_translate_x100.fbx";

  const char* kFbxAscii
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "GlobalSettings:  {\n"
      "  Version: 1000\n"
      "  Properties70:  {\n"
      "    P: \"UpAxis\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"UpAxisSign\", \"int\", \"Integer\", \"\", -1\n"
      "    P: \"FrontAxis\", \"int\", \"Integer\", \"\", 2\n"
      "    P: \"FrontAxisSign\", \"int\", \"Integer\", \"\", -1\n"
      "    P: \"CoordAxis\", \"int\", \"Integer\", \"\", 0\n"
      "    P: \"CoordAxisSign\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"UnitScaleFactor\", \"double\", \"Number\", \"\", 1\n"
      "  }\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 2\n"
      "  ObjectType: \"Model\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Geometry\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Model: 1, \"Model::Triangle\", \"Mesh\" {\n"
      "    Properties70:  {\n"
      "      P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", "
      "100,0,0\n"
      "    }\n"
      "  }\n"
      "  Geometry: 2, \"Geometry::Triangle\", \"Mesh\" {\n"
      "    Vertices: *9 {\n"
      "      a: 0,0,0,  1,0,0,  0,1,0\n"
      "    }\n"
      "    PolygonVertexIndex: *3 {\n"
      "      a: 0,1,-3\n"
      "    }\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OO\", 2, 1\n"
      "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.import_content
    = ImportContentFlags::kGeometry | ImportContentFlags::kScene;
  request.options.coordinate.unit_normalization
    = UnitNormalizationPolicy::kNormalizeToMeters;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.scenes_written, 1u);

  const auto scene = LoadSceneDescriptor(report.cooked_root);
  const auto node_opt = FindNodeByNameSuffix(scene, "Triangle");
  ASSERT_TRUE(node_opt.has_value());

  const auto& node = *node_opt;
  EXPECT_NEAR(node.translation[0], 1.0F, 1e-5F);
  EXPECT_NEAR(node.translation[1], 0.0F, 1e-5F);
  EXPECT_NEAR(node.translation[2], 0.0F, 1e-5F);
}

//! Test: PreserveSource leaves node translations unchanged.
/*!
 Scenario: An ASCII FBX declares centimeter units (UnitScaleFactor=1). A node
 translation is authored as 100 units.

 Verifies: With UnitNormalizationPolicy::kPreserveSource, the emitted scene
 node translation remains 100.
*/
NOLINT_TEST_F(FbxImporterUnitsAxesTest, RealBackend_PreservesSourceUnits)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_units_preserve_source");
  const auto source_path = temp_dir / "units_cm_translate_x100.fbx";

  const char* kFbxAscii
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "GlobalSettings:  {\n"
      "  Version: 1000\n"
      "  Properties70:  {\n"
      "    P: \"UpAxis\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"UpAxisSign\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"FrontAxis\", \"int\", \"Integer\", \"\", 2\n"
      "    P: \"FrontAxisSign\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"CoordAxis\", \"int\", \"Integer\", \"\", 0\n"
      "    P: \"CoordAxisSign\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"UnitScaleFactor\", \"double\", \"Number\", \"\", 1\n"
      "  }\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 2\n"
      "  ObjectType: \"Model\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Geometry\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Model: 1, \"Model::Triangle\", \"Mesh\" {\n"
      "    Properties70:  {\n"
      "      P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", "
      "100,0,0\n"
      "    }\n"
      "  }\n"
      "  Geometry: 2, \"Geometry::Triangle\", \"Mesh\" {\n"
      "    Vertices: *9 {\n"
      "      a: 0,0,0,  1,0,0,  0,1,0\n"
      "    }\n"
      "    PolygonVertexIndex: *3 {\n"
      "      a: 0,1,-3\n"
      "    }\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OO\", 2, 1\n"
      "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.import_content
    = ImportContentFlags::kGeometry | ImportContentFlags::kScene;
  request.options.coordinate.unit_normalization
    = UnitNormalizationPolicy::kPreserveSource;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.scenes_written, 1u);

  const auto scene = LoadSceneDescriptor(report.cooked_root);
  const auto node_opt = FindNodeByNameSuffix(scene, "Triangle");
  ASSERT_TRUE(node_opt.has_value());

  const auto& node = *node_opt;
  EXPECT_NEAR(node.translation[0], 100.0F, 1e-5F);
  EXPECT_NEAR(node.translation[1], 0.0F, 1e-5F);
  EXPECT_NEAR(node.translation[2], 0.0F, 1e-5F);
}

//! Test: ApplyCustomFactor scales meters by the custom multiplier.
/*!
 Scenario: An ASCII FBX declares centimeter units (UnitScaleFactor=1). A node
 translation is authored as 100 units (100 cm = 1 meter).

 Verifies: With UnitNormalizationPolicy::kApplyCustomFactor and
 custom_unit_scale=2, the emitted scene node translation is 2.
*/
NOLINT_TEST_F(FbxImporterUnitsAxesTest, RealBackend_AppliesCustomUnitScale)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_units_custom_factor");
  const auto source_path = temp_dir / "units_cm_translate_x100.fbx";

  const char* kFbxAscii
    = "; FBX 7.4.0 project file\n"
      "FBXHeaderExtension:  {\n"
      "  FBXHeaderVersion: 1003\n"
      "  FBXVersion: 7400\n"
      "  Creator: \"OxygenTests\"\n"
      "}\n"
      "GlobalSettings:  {\n"
      "  Version: 1000\n"
      "  Properties70:  {\n"
      "    P: \"UpAxis\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"UpAxisSign\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"FrontAxis\", \"int\", \"Integer\", \"\", 2\n"
      "    P: \"FrontAxisSign\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"CoordAxis\", \"int\", \"Integer\", \"\", 0\n"
      "    P: \"CoordAxisSign\", \"int\", \"Integer\", \"\", 1\n"
      "    P: \"UnitScaleFactor\", \"double\", \"Number\", \"\", 1\n"
      "  }\n"
      "}\n"
      "Definitions:  {\n"
      "  Version: 100\n"
      "  Count: 2\n"
      "  ObjectType: \"Model\" {\n"
      "    Count: 1\n"
      "  }\n"
      "  ObjectType: \"Geometry\" {\n"
      "    Count: 1\n"
      "  }\n"
      "}\n"
      "Objects:  {\n"
      "  Model: 1, \"Model::Triangle\", \"Mesh\" {\n"
      "    Properties70:  {\n"
      "      P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\", "
      "100,0,0\n"
      "    }\n"
      "  }\n"
      "  Geometry: 2, \"Geometry::Triangle\", \"Mesh\" {\n"
      "    Vertices: *9 {\n"
      "      a: 0,0,0,  1,0,0,  0,1,0\n"
      "    }\n"
      "    PolygonVertexIndex: *3 {\n"
      "      a: 0,1,-3\n"
      "    }\n"
      "  }\n"
      "}\n"
      "Connections:  {\n"
      "  C: \"OO\", 2, 1\n"
      "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.import_content
    = ImportContentFlags::kGeometry | ImportContentFlags::kScene;
  request.options.coordinate.unit_normalization
    = UnitNormalizationPolicy::kApplyCustomFactor;
  request.options.coordinate.custom_unit_scale = 2.0F;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.scenes_written, 1u);

  const auto scene = LoadSceneDescriptor(report.cooked_root);
  const auto node_opt = FindNodeByNameSuffix(scene, "Triangle");
  ASSERT_TRUE(node_opt.has_value());

  const auto& node = *node_opt;
  EXPECT_NEAR(node.translation[0], 2.0F, 1e-5F);
  EXPECT_NEAR(node.translation[1], 0.0F, 1e-5F);
  EXPECT_NEAR(node.translation[2], 0.0F, 1e-5F);
}

//! Test: Axis conversion maps Y-up authored translation into Oxygen Z-up.
/*!
 Scenario: An ASCII FBX declares a Y-up axis system. A node translation is
 authored along +Y.

 Verifies: The emitted scene node translation is along +Z in Oxygen space.
*/
NOLINT_TEST_F(FbxImporterUnitsAxesTest, RealBackend_SwapYZAxes_SwapsTranslation)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_axes_swap_yz_translation");
  const auto source_path = temp_dir / "translate_y10_z20.fbx";

  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 2\n"
                          "  ObjectType: \"Model\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "  ObjectType: \"Geometry\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Model: 1, \"Model::Triangle\", \"Mesh\" {\n"
                          "    Properties70:  {\n"
                          "      P: \"Lcl Translation\", \"Lcl Translation\", "
                          "\"\", \"A\", 0,10,20\n"
                          "    }\n"
                          "  }\n"
                          "  Geometry: 2, \"Geometry::Triangle\", \"Mesh\" {\n"
                          "    Vertices: *9 {\n"
                          "      a: 0,0,0,  1,0,0,  0,1,0\n"
                          "    }\n"
                          "    PolygonVertexIndex: *3 {\n"
                          "      a: 0,1,-3\n"
                          "    }\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
                          "  C: \"OO\", 2, 1\n"
                          "}\n";

  WriteTextFile(source_path, kFbxAscii);

  AssetImporter importer;

  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.import_content
    = ImportContentFlags::kGeometry | ImportContentFlags::kScene;
  request.options.coordinate.unit_normalization
    = UnitNormalizationPolicy::kPreserveSource;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  ASSERT_TRUE(report.success);

  const auto scene = LoadSceneDescriptor(report.cooked_root);
  const auto node_opt = FindNodeByNameSuffix(scene, "Triangle");
  ASSERT_TRUE(node_opt.has_value());
}

} // namespace

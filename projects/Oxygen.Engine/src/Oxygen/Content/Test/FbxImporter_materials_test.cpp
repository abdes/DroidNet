//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <filesystem>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>

#include "FbxImporterTest.h"

namespace {

using oxygen::content::LooseCookedInspection;
using oxygen::content::import::AssetImporter;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::data::AssetType;
using oxygen::data::pak::MaterialAssetDesc;

class FbxImporterMaterialsTest : public oxygen::content::test::FbxImporterTest {
};

//! Test: Real FBX backend emits materials from a minimal ASCII FBX.
/*!\
 Scenario: Writes a minimal ASCII FBX containing one material object.
 Runs the default AssetImporter() (wired to the real FbxImporter backend).
 Verifies the import emits a loadable loose cooked index with exactly one
 material descriptor of PakFormat size.

 @note The test uses NormalizeNamingStrategy to ensure any FBX-authored
  material names are safe for container-relative descriptor paths.
*/
NOLINT_TEST_F(
  FbxImporterMaterialsTest, RealBackend_EmitsMaterial_FromAsciiFbxFixture)
{
  // Arrange
  const auto temp_dir = MakeTempDir("fbx_importer_real_ascii");
  const auto source_path = temp_dir / "scene.fbx";

  // Minimal FBX ASCII with a single material.
  // This intentionally contains a name with ':' to validate path
  // sanitization via NormalizeNamingStrategy.
  const char* kFbxAscii = "; FBX 7.4.0 project file\n"
                          "FBXHeaderExtension:  {\n"
                          "  FBXHeaderVersion: 1003\n"
                          "  FBXVersion: 7400\n"
                          "  Creator: \"OxygenTests\"\n"
                          "}\n"
                          "Definitions:  {\n"
                          "  Version: 100\n"
                          "  Count: 1\n"
                          "  ObjectType: \"Material\" {\n"
                          "    Count: 1\n"
                          "  }\n"
                          "}\n"
                          "Objects:  {\n"
                          "  Material: 1, \"Material::TestMat\", \"\" {\n"
                          "    Version: 102\n"
                          "    ShadingModel: \"phong\"\n"
                          "    MultiLayer: 0\n"
                          "  }\n"
                          "}\n"
                          "Connections:  {\n"
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
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content
    = oxygen::content::import::ImportContentFlags::kMaterials;

  // Act
  const auto report = importer.ImportToLooseCooked(request);

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.materials_written, 1u);

  LooseCookedInspection inspection;
  inspection.LoadFromRoot(report.cooked_root);

  ASSERT_EQ(inspection.Assets().size(), 1u);
  const auto& asset = inspection.Assets().front();
  EXPECT_EQ(asset.descriptor_size, sizeof(MaterialAssetDesc));
  EXPECT_EQ(asset.asset_type, static_cast<uint8_t>(AssetType::kMaterial));
}

} // namespace

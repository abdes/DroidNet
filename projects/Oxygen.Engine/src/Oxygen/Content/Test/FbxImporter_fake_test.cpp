//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportFormat.h>
#include <Oxygen/Content/Import/Importer.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

#include "FbxImporterTest.h"

namespace {

using oxygen::content::LooseCookedInspection;
using oxygen::content::import::AssetImporter;
using oxygen::content::import::CookedContentWriter;
using oxygen::content::import::Importer;
using oxygen::content::import::ImportFormat;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::data::AssetKey;
using oxygen::data::AssetType;
using oxygen::data::pak::MaterialAssetDesc;
using oxygen::serio::MemoryStream;
using oxygen::serio::Writer;

class FakeFbxImporter final : public Importer {
public:
  [[nodiscard]] auto Name() const noexcept -> std::string_view override
  {
    return "FakeFbxImporter";
  }

  [[nodiscard]] auto Supports(const ImportFormat format) const noexcept
    -> bool override
  {
    return format == ImportFormat::kFbx;
  }

  auto Import(const ImportRequest& request, CookedContentWriter& out)
    -> void override
  {
    AssetKey key {};
    key.guid = std::array<uint8_t, 16> {
      0xAA,
      0xBB,
      0xCC,
      0xDD,
      0x00,
      0x01,
      0x02,
      0x03,
      0x04,
      0x05,
      0x06,
      0x07,
      0x08,
      0x09,
      0x10,
      0x11,
    };

    const auto virtual_path
      = request.loose_cooked_layout.MaterialVirtualPath("M_Test");

    const auto descriptor_relpath
      = request.loose_cooked_layout.MaterialDescriptorRelPath("M_Test");

    MaterialAssetDesc desc {};

    MemoryStream stream;
    Writer<MemoryStream> writer(stream);
    (void)writer.WriteBlob(
      std::as_bytes(std::span<const MaterialAssetDesc, 1>(&desc, 1)));

    const auto bytes = stream.Data();
    out.WriteAssetDescriptor(
      key, AssetType::kMaterial, virtual_path, descriptor_relpath, bytes);
    out.OnMaterialsWritten(1);
  }
};

class FbxImporterFakeBackendTest
  : public oxygen::content::test::FbxImporterTest { };

//! Test: AssetImporter runs a backend and emits a valid loose cooked index.
/*!\
 Scenario: Uses dependency injection to supply a fake FBX backend that emits
 one PakFormat-sized MaterialAssetDesc descriptor serialized via Serio.
 Verifies the resulting container index is loadable and references the
 emitted descriptor.
*/
NOLINT_TEST_F(
  FbxImporterFakeBackendTest, ImportToLooseCooked_EmitsLoadableIndex)
{
  // Arrange
  auto backends = std::vector<std::unique_ptr<Importer>> {};
  backends.push_back(std::make_unique<FakeFbxImporter>());
  AssetImporter importer(std::move(backends));

  const auto temp_dir = MakeTempDir("fbx_importer_basic");
  const auto source_path = temp_dir / "scene.fbx";
  WriteTextFile(source_path, "");

  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir / "cooked",
    .loose_cooked_layout = LooseCookedLayout {},
  };

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

  const auto expected_virtual_path
    = request.loose_cooked_layout.MaterialVirtualPath("M_Test");
  EXPECT_EQ(asset.virtual_path, expected_virtual_path);

  const auto expected_relpath
    = request.loose_cooked_layout.MaterialDescriptorRelPath("M_Test");
  EXPECT_EQ(asset.descriptor_relpath, expected_relpath);
}

} // namespace

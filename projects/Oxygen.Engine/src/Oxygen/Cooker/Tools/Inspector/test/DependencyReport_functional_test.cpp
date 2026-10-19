//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/Inspector/DependencyReport.cpp

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/test/Fixtures/LooseCookedTestWriter.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/Tools/Inspector/DependencyReport.h>
#include <Oxygen/Cooker/test/Pak/PakTestSupport.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
using oxygen::content::inspection::RunDependencyReport;

class InspectorDependencyReportTest : public pak::test::TempDirFixture {
protected:
  //! Writes a loose root whose owner material references a shared material.
  auto WriteRoot(const std::filesystem::path& root) -> void
  {
    auto descriptor = data::pak::render::MaterialAssetDesc {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kMaterial);
    descriptor.header.version = data::pak::render::kMaterialAssetVersion;
    const auto bytes = data::MakeDescriptorBytes(descriptor);
    const auto none = data::AssetReferences::Create({}, {});
    const auto owner = data::AssetReferences::Create({},
      {
        {
          .key = shared_,
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kMaterial,
        },
      });
    ASSERT_TRUE(none.has_value() && owner.has_value());
    auto writer = oxygen::content::testing::LooseCookedTestWriter(root);
    writer.WriteAssetDescriptor(shared_, data::AssetType::kMaterial,
      "/Content/Shared.omat", "Shared.omat", bytes, *none);
    writer.WriteAssetDescriptor(owner_, data::AssetType::kMaterial,
      "/Content/Owner.omat", "Owner.omat", bytes, *owner);
    static_cast<void>(writer.Finish());
  }

  auto Report(const std::filesystem::path& source) -> nlohmann::json
  {
    const auto output = Path("dependencies.json");
    EXPECT_EQ(RunDependencyReport({
                .cooked_root = source.string(),
                .output = output.string(),
              }),
      0);
    auto stream = std::ifstream(output);
    return nlohmann::json::parse(stream);
  }

  data::AssetKey shared_ = pak::test::MakeAssetKey(1U);
  data::AssetKey owner_ = pak::test::MakeAssetKey(2U);
};

//! A pak reports the same direct dependencies as the loose root it packs.
NOLINT_TEST_F(InspectorDependencyReportTest, ReportsDependenciesFromAPak)
{
  // Arrange
  const auto loose = Path("loose");
  WriteRoot(loose);
  const auto pak_path = Path("content.pak");
  const auto built
    = pak::PakBuilder {}.Build(pak::test::MakeFullRequest(pak_path,
      {
        .sources = { data::CookedSource {
          .kind = data::CookedSourceKind::kLooseCooked,
          .path = loose,
        } },
        .embed_browse_index = true,
      }));
  ASSERT_TRUE(built.has_value() && built->summary.diagnostics_error == 0U);
  const auto expected = Report(loose);

  // Act
  const auto actual = Report(pak_path);

  // Assert
  ASSERT_EQ(actual.at("assets").size(), expected.at("assets").size());
  for (const auto& row : expected.at("assets")) {
    const auto match = std::ranges::find_if(
      actual.at("assets"), [&row](const nlohmann::json& candidate) -> bool {
        return candidate.at("asset_key") == row.at("asset_key");
      });
    ASSERT_NE(match, actual.at("assets").end());
    EXPECT_EQ(match->at("virtual_path"), row.at("virtual_path"));
    EXPECT_EQ(match->at("dependencies"), row.at("dependencies"));
    EXPECT_EQ(match->at("key_references"), row.at("key_references"));
  }
}

} // namespace

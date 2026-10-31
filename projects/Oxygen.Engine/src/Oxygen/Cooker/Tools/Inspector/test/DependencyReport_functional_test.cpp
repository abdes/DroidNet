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
#include <iostream>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Inspector/DependencyReport.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
using oxygen::content::inspection::DependencyReportOptions;

//! `RunDependencyReport` as the tool runs it: the error, if any, goes to
//! stderr, and the result is the process exit code.
auto RunDependencyReport(const DependencyReportOptions& options) -> int
{
  return oxygen::content::toolkit::ExitCodeOf(std::cerr,
    [&] { return oxygen::content::inspection::RunDependencyReport(options); });
}

class InspectorDependencyReportTest : public oxygen::testing::TempDirTest {
protected:
  //! A library whose owner material references a shared material.
  [[nodiscard]] auto MakeLibrary() const
    -> oxygen::cooker::testing::LooseCookedRoot
  {
    auto descriptor = data::pak::render::MaterialAssetDesc {};
    descriptor.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kMaterial);
    descriptor.header.version = data::pak::render::kMaterialAssetVersion;
    const auto bytes = data::MakeDescriptorBytes(descriptor);
    auto library = oxygen::cooker::testing::LooseCookedRoot { "Content" };
    const auto none = data::AssetReferences::Create({}, {});
    const auto owner = data::AssetReferences::Create({},
      {
        {
          .key = library.KeyOf("Shared.omat"),
          .kind = data::KeyReferenceKind::kAsset,
          .expected_type = data::AssetType::kMaterial,
        },
      });
    EXPECT_TRUE(none.has_value() && owner.has_value());
    library.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "Shared.omat",
      .descriptor = bytes,
      .references = *none,
    });
    library.Add({
      .type = data::AssetType::kMaterial,
      .leaf = "Owner.omat",
      .descriptor = bytes,
      .references = *owner,
    });
    return library;
  }

  auto Report(const std::filesystem::path& source) -> nlohmann::json
  {
    const auto output = TempPath("dependencies.json");
    EXPECT_EQ(RunDependencyReport({
                .cooked_root = source.string(),
                .output = output.string(),
              }),
      0);
    auto stream = std::ifstream(output);
    return nlohmann::json::parse(stream);
  }
};

//! A pak reports the same direct dependencies as the loose root it packs.
NOLINT_TEST_F(InspectorDependencyReportTest, ReportsDependenciesFromAPak)
{
  // Arrange
  const auto library = MakeLibrary();
  const auto loose = TempPath("loose");
  oxygen::cooker::testing::Materialize(library.Files(), loose);
  auto workspace = oxygen::cooker::testing::PakWorkspace {};
  workspace.PackOrThrow(workspace.FullRequest({ workspace.Add(library) }));
  const auto pak_path = TempPath("content.pak");
  oxygen::cooker::testing::Materialize(
    workspace.Storage().Bytes(
      oxygen::cooker::testing::PakWorkspace::PakPath("out.pak")),
    pak_path);
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

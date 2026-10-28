//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <set>
#include <sstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/CookedVersions.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Importer.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::CookedVersionsReport;
using oxygen::content::PrintCookedVersions;
using oxygen::content::import::Importer;
using oxygen::content::import::ImporterOf;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::kImporters;
using oxygen::content::import::RevisionOf;
using oxygen::content::import::ScriptingImportKind;

[[nodiscard]] auto RequestFor(const std::string_view source) -> ImportRequest
{
  auto request = ImportRequest {};
  request.source_path = std::string(source);
  return request;
}

//! A source file is cooked by its format's importer.
NOLINT_TEST(ImporterTest, SourceFormatSelectsItsImporter)
{
  EXPECT_EQ(ImporterOf(RequestFor("Models/Ship.glb")), Importer::kGltf);
  EXPECT_EQ(ImporterOf(RequestFor("Models/Ship.gltf")), Importer::kGltf);
  EXPECT_EQ(ImporterOf(RequestFor("Models/Ship.FBX")), Importer::kFbx);
  EXPECT_EQ(ImporterOf(RequestFor("Textures/Wood.png")), Importer::kTexture);
  EXPECT_FALSE(ImporterOf(RequestFor("Notes/Readme.txt")).has_value());
}

//! A request's payload selects the importer that cooks it, whatever its
//! source's extension.
NOLINT_TEST(ImporterTest, PayloadSelectsItsImporter)
{
  auto material = RequestFor("Materials/Wood.material.json");
  material.material_descriptor.emplace();
  EXPECT_EQ(ImporterOf(material), Importer::kMaterialDescriptor);

  auto geometry = RequestFor("Geometry/Rock.geometry.json");
  geometry.geometry_descriptor.emplace();
  EXPECT_EQ(ImporterOf(geometry), Importer::kGeometryDescriptor);

  auto scene = RequestFor("Scenes/Main.scene.json");
  scene.scene_descriptor.emplace();
  EXPECT_EQ(ImporterOf(scene), Importer::kSceneDescriptor);

  auto input = RequestFor("Input/Player.input.json");
  input.input.emplace();
  EXPECT_EQ(ImporterOf(input), Importer::kInput);

  auto script = RequestFor("Scripts/Door.luau");
  script.options.scripting.import_kind = ScriptingImportKind::kScriptAsset;
  EXPECT_EQ(ImporterOf(script), Importer::kScriptAsset);

  auto sidecar = RequestFor("Scenes/Main.scripting.json");
  sidecar.options.scripting.import_kind
    = ScriptingImportKind::kScriptingSidecar;
  EXPECT_EQ(ImporterOf(sidecar), Importer::kScriptingSidecar);

  auto physics = RequestFor("Scenes/Main.physics.json");
  physics.physics.emplace();
  EXPECT_EQ(ImporterOf(physics), Importer::kPhysicsSidecar);
}

//! Every importer has its own name and a revision from 1.
NOLINT_TEST(ImporterTest, ImportersHaveDistinctNamesAndRevisions)
{
  auto names = std::set<std::string_view> {};
  for (const auto importer : kImporters) {
    EXPECT_TRUE(names.insert(nostd::to_string(importer)).second)
      << nostd::to_string(importer);
    EXPECT_GE(RevisionOf(importer), 1U) << nostd::to_string(importer);
  }
}

//! A cooking tool reports every format, asset and importer of the catalog.
NOLINT_TEST(CookedVersionsTest, JsonReportsTheCatalog)
{
  auto out = std::ostringstream {};

  PrintCookedVersions(out,
    CookedVersionsReport {
      .program = "tool",
      .version = "1.0",
      .importers = true,
      .json = true,
    });

  const auto document = nlohmann::json::parse(out.str());
  EXPECT_EQ(document.at("program"), "tool");
  EXPECT_EQ(document.at("version"), "1.0");
  EXPECT_EQ(document.at("formats").at("loose_cooked_index"),
    oxygen::data::pak::version::kLooseCookedIndexVersion);
  EXPECT_EQ(document.at("assets").at("geometry"),
    oxygen::data::pak::version::kGeometryAssetVersion);
  EXPECT_EQ(document.at("importers").size(), kImporters.size());
  EXPECT_EQ(document.at("importers").at("gltf"), RevisionOf(Importer::kGltf));
}

//! A tool that only reads cooked content reports no importers.
NOLINT_TEST(CookedVersionsTest, ReaderReportsNoImporters)
{
  auto out = std::ostringstream {};

  PrintCookedVersions(out,
    CookedVersionsReport { .program = "tool", .version = "1.0", .json = true });

  EXPECT_FALSE(nlohmann::json::parse(out.str()).contains("importers"));
}

//! The text form lists each group with its versions aligned.
NOLINT_TEST(CookedVersionsTest, TextListsEachGroup)
{
  auto out = std::ostringstream {};

  PrintCookedVersions(
    out, CookedVersionsReport { .program = "tool", .version = "1.0" });

  const auto text = out.str();
  EXPECT_TRUE(text.starts_with("tool version 1.0\n"));
  EXPECT_NE(text.find("\nformats\n"), std::string::npos);
  EXPECT_NE(text.find("\nassets\n"), std::string::npos);
  EXPECT_EQ(text.find("\nimporters\n"), std::string::npos);
}

} // namespace

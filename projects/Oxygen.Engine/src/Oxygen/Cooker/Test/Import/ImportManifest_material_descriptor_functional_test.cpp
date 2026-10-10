//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/ImportManifest.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::ImportManifest;
using oxygen::cooker::test::kContentHashingDefault;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  BuildsMaterialDescriptorRequestWithDefaultsAndDescriptorOverrides)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "builds_material_descriptor" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Materials" / "wood.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "WoodFloor",
      "content_hashing": false,
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Textures/WoodFloor_Color.otex"
        }
      }
    })");

  const auto cooked_root = (root / ".cooked").generic_string();
  const auto manifest_json = std::string { R"({
      "version": 1,
      "output": ")" }
    + cooked_root + R"(",
      "defaults": {
        "material_descriptor": {
          "content_hashing": true,
          "name": "default-material-name"
        }
      },
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/wood.material.json",
          "name": "wood-job",
          "content_hashing": true
        }
      ]
    })";
  WriteText(manifest_path, manifest_json);

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name, std::optional<std::string> { "wood-job" });
  ASSERT_TRUE(request->material_descriptor.has_value());

  // Descriptor intent overrides defaults; Release enforces content hashing.
  EXPECT_EQ(request->options.with_content_hashing, kContentHashingDefault);

  const auto normalized
    = json::parse(request->material_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "WoodFloor");
}

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  AppliesSharedLayoutOverridesToMaterialDescriptorRequests)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "material_shared_layout" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Materials" / "wood.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "WoodFloor",
      "domain": "opaque"
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "layout": {
        "materials_subdir": "DescriptorMaterialsTop"
      },
      "defaults": {
        "layout": {
          "materials_subdir": "DescriptorMaterialsDefault"
        }
      },
      "output": ")"
      + (manifest_path.parent_path() / "material-layout-cooked")
        .generic_string()
      + R"(",
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/wood.material.json"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  EXPECT_EQ(request->loose_cooked_layout.materials_subdir,
    "DescriptorMaterialsDefault");
}

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  CollectsAndPropagatesMaterialDescriptorDependencies)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "collects_material_dependencies" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto material_descriptor_path
    = root / "Materials" / "wood.material.json";
  WriteText(material_descriptor_path,
    R"({
      "name": "WoodFloor",
      "domain": "opaque"
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "material-cooked").generic_string()
      + R"(",
      "jobs": [
        {
          "id": "wood.color",
          "type": "texture-descriptor",
          "source": "woodfloor007_color.texture.json"
        },
        {
          "id": "wood.material",
          "type": "material-descriptor",
          "source": "Materials/wood.material.json",
          "depends_on": ["wood.color"]
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 2U);
  EXPECT_EQ(manifest->jobs[1].id, "wood.material");
  ASSERT_EQ(manifest->jobs[1].depends_on.size(), 1U);
  EXPECT_EQ(manifest->jobs[1].depends_on[0], "wood.color");

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[1].BuildRequest(request_errors);
  ASSERT_TRUE(request.has_value()) << request_errors.str();
  ASSERT_TRUE(request->orchestration.has_value());
  EXPECT_EQ(request->orchestration->job_id, "wood.material");
  ASSERT_EQ(request->orchestration->depends_on.size(), 1U);
  EXPECT_EQ(request->orchestration->depends_on[0], "wood.color");
}

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  RejectsMaterialDescriptorJobWithDisallowedKeys)
{
  const ScopedTempDir temp;
  const auto manifest_path
    = temp.Path() / "rejects_disallowed_key" / "import_manifest.json";
  WriteText(manifest_path,
    R"({
      "version": 1,
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/wood.material.json",
          "intent": "albedo"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  EXPECT_FALSE(manifest.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("manifest schema validation failed"));
}

NOLINT_TEST(ImportManifestMaterialDescriptorTest,
  RejectsDescriptorPayloadThatViolatesDescriptorSchema)
{
  const ScopedTempDir temp;
  const auto manifest_path = temp.Path()
    / "rejects_bad_material_descriptor_payload" / "import_manifest.json";
  const auto root = manifest_path.parent_path();
  const auto descriptor_path = root / "Materials" / "bad.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "Bad",
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Textures/WoodFloor_Color.otex",
          "bad_key": 1
        }
      }
    })");

  WriteText(manifest_path,
    R"({
      "version": 1,
      "output": ")"
      + (manifest_path.parent_path() / "material-cooked").generic_string()
      + R"(",
      "jobs": [
        {
          "type": "material-descriptor",
          "source": "Materials/bad.material.json"
        }
      ]
    })");

  auto errors = std::ostringstream {};
  const auto manifest
    = ImportManifest::Load(manifest_path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  ASSERT_EQ(manifest->jobs.size(), 1U);

  auto request_errors = std::ostringstream {};
  const auto request = manifest->jobs[0].BuildRequest(request_errors);
  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(request_errors.str(),
    ::testing::HasSubstr("material.descriptor.schema_validation_failed"));
}

//! Per-job folders preserve nested identities without changing the shared
//! container.
NOLINT_TEST(
  ImportManifestMaterialDescriptorTest, JobLayoutOverridesRetainGlobalDefaults)
{
  const ScopedTempDir temp;
  const auto path
    = temp.Path() / "job_layout_overrides" / "import_manifest.json";
  WriteText(
    path.parent_path() / "shared.material.json", R"({"name":"Shared"})");
  const json document = { { "version", 1 },
    { "output", (path.parent_path() / ".cooked").generic_string() },
    { "layout",
      { { "virtual_mount_root", "/Content" },
        { "descriptors_dir", "Descriptors" },
        { "materials_subdir", "Common" } } },
    { "jobs",
      json::array({ { { "type", "material-descriptor" },
                      { "source", "shared.material.json" },
                      { "layout",
                        { { "descriptors_dir", "" },
                          { "materials_subdir", "Materials/A" } } } },
        { { "type", "material-descriptor" },
          { "source", "shared.material.json" },
          { "layout",
            { { "descriptors_dir", "" },
              { "materials_subdir", "Materials/B" } } } },
        { { "type", "material-descriptor" },
          { "source", "shared.material.json" } } }) } };
  WriteText(path, document.dump());
  std::ostringstream errors;
  const auto manifest = ImportManifest::Load(path, std::nullopt, errors);
  ASSERT_TRUE(manifest.has_value()) << errors.str();
  const auto requests = manifest->BuildRequests(errors);
  ASSERT_EQ(requests.size(), 3U) << errors.str();
  EXPECT_EQ(requests[0].loose_cooked_layout.MaterialVirtualPath("Shared"),
    "/Content/Materials/A/Shared.omat");
  EXPECT_EQ(requests[1].loose_cooked_layout.MaterialVirtualPath("Shared"),
    "/Content/Materials/B/Shared.omat");
  EXPECT_EQ(requests[2].loose_cooked_layout.MaterialVirtualPath("Shared"),
    "/Content/Descriptors/Common/Shared.omat");
  EXPECT_EQ(requests[0].cooked_root, requests[1].cooked_root);
  EXPECT_EQ(requests[0].loose_cooked_layout.resources_dir,
    requests[2].loose_cooked_layout.resources_dir);
}

//! Malformed job overrides are rejected before job construction.
NOLINT_TEST(ImportManifestMaterialDescriptorTest, RejectsMalformedJobLayout)
{
  const ScopedTempDir temp;
  const auto path = temp.Path() / "invalid_job_layout" / "import_manifest.json";
  for (const auto& layout :
    { json("invalid"), json { { "materials_subdir", 12 } } }) {
    WriteText(path,
      json { { "version", 1 },
        { "output", (path.parent_path() / ".cooked").generic_string() },
        { "jobs",
          json::array({ { { "type", "material-descriptor" },
            { "source", "shared.material.json" }, { "layout", layout } } }) } }
        .dump());
    std::ostringstream errors;
    EXPECT_FALSE(ImportManifest::Load(path, std::nullopt, errors).has_value());
    EXPECT_FALSE(errors.str().empty());
  }
}

} // namespace

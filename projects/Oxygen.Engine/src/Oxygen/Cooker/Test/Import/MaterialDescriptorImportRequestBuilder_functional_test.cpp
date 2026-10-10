//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/MaterialDescriptorImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/MaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::MaterialDescriptorImportSettings;
using oxygen::content::import::internal::BuildMaterialDescriptorRequest;
using oxygen::cooker::test::kContentHashingDefault;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> MaterialDescriptorImportSettings
{
  auto settings = MaterialDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-material";
  settings.with_content_hashing = true;
  return settings;
}

NOLINT_TEST(MaterialDescriptorImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorWithNormalizedPayload)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "valid_request";
  const auto descriptor_path = dir / "Materials" / "wood.material.json";
  WriteText(descriptor_path,
    R"({
      "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json",
      "name": "WoodFloor",
      "content_hashing": false,
      "domain": "opaque",
      "alpha_mode": "opaque",
      "orm_policy": "auto",
      "parameters": {
        "metalness": 0.2,
        "roughness": 0.7
      },
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Textures/WoodFloor_Color.otex",
          "uv_set": 0
        }
      }
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildMaterialDescriptorRequest(settings, errors);

  ASSERT_HAS_VALUE(request) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "manifest-material" });
  EXPECT_EQ(request->options.with_content_hashing, kContentHashingDefault);
  ASSERT_HAS_VALUE(request->material_descriptor);

  const auto normalized
    = json::parse(request->material_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "WoodFloor");
  EXPECT_EQ(normalized.at("textures")
              .at("base_color")
              .at("virtual_path")
              .get<std::string>(),
    "/.cooked/Textures/WoodFloor_Color.otex");
}

NOLINT_TEST(MaterialDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "schema_violation";
  const auto descriptor_path = dir / "Materials" / "bad.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "Bad",
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Textures/WoodFloor_Color.otex",
          "unexpected": true
        }
      }
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("material.descriptor.schema_validation_failed"));
}

NOLINT_TEST(MaterialDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Materials" / "missing.material.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("failed to open material descriptor"));
}

NOLINT_TEST(
  MaterialDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "relative_output";
  const auto descriptor_path = dir / "Materials" / "ok.material.json";
  WriteText(descriptor_path, R"({ "name": "WoodFloor" })");

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("cooked root must be an absolute path"));
}

} // namespace

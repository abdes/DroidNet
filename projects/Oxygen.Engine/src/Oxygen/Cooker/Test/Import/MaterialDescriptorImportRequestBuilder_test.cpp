//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/MaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h>

namespace {

using nlohmann::json;
using oxygen::content::import::MaterialDescriptorImportSettings;
using oxygen::content::import::internal::BuildMaterialDescriptorRequest;

auto MakeTempDir(const std::string_view stem) -> std::filesystem::path
{
  auto dir = std::filesystem::temp_directory_path() / "oxygen_material_desc";
  dir /= std::filesystem::path { std::string { stem } };
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}

auto WriteTextFile(
  const std::filesystem::path& path, const std::string_view text) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.is_open());
  out << text;
}

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
  const auto dir = MakeTempDir("valid_request");
  const auto descriptor_path = dir / "Materials" / "wood.material.json";
  WriteTextFile(descriptor_path,
    R"({
      "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json",
      "name": "WoodFloor",
      "content_hashing": false,
      "domain": "opaque",
      "alpha_mode": "opaque",
      "orm_policy": "auto",
      "inputs": {
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

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(
    request->job_name, std::optional<std::string> { "manifest-material" });
  EXPECT_FALSE(oxygen::content::import::EffectiveContentHashingEnabled(
    request->options.with_content_hashing));
  ASSERT_TRUE(request->options.material_descriptor.has_value());

  const auto normalized = json::parse(
    request->options.material_descriptor->normalized_descriptor_json);
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
  const auto dir = MakeTempDir("schema_violation");
  const auto descriptor_path = dir / "Materials" / "bad.material.json";
  WriteTextFile(descriptor_path,
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
  EXPECT_TRUE(errors.str().find("material.descriptor.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(MaterialDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const auto dir = MakeTempDir("missing_file");
  const auto descriptor_path = dir / "Materials" / "missing.material.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("failed to open material descriptor")
    != std::string::npos);
}

NOLINT_TEST(
  MaterialDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const auto dir = MakeTempDir("relative_output");
  const auto descriptor_path = dir / "Materials" / "ok.material.json";
  WriteTextFile(descriptor_path, R"({ "name": "WoodFloor" })");

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("cooked root must be an absolute path")
    != std::string::npos);
}

} // namespace

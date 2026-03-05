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

#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportSettings.h>

namespace {

using nlohmann::json;
using oxygen::content::import::PhysicsMaterialDescriptorImportSettings;
using oxygen::content::import::internal::BuildPhysicsMaterialDescriptorRequest;

auto MakeTempDir(const std::string_view stem) -> std::filesystem::path
{
  auto dir
    = std::filesystem::temp_directory_path() / "oxygen_physics_material_desc";
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
  -> PhysicsMaterialDescriptorImportSettings
{
  auto settings = PhysicsMaterialDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-physics-material";
  settings.with_content_hashing = true;
  return settings;
}

NOLINT_TEST(PhysicsMaterialDescriptorImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorWithNormalizedPayload)
{
  const auto dir = MakeTempDir("valid_request");
  const auto descriptor_path = dir / "Physics" / "ground.material.json";
  WriteTextFile(descriptor_path,
    R"({
      "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-material-descriptor.schema.json",
      "name": "ground",
      "content_hashing": false,
      "static_friction": 0.95,
      "dynamic_friction": 0.70,
      "restitution": 0.05,
      "density": 1800.0,
      "combine_mode_friction": "max",
      "combine_mode_restitution": "average",
      "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsMaterialDescriptorRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name,
    std::optional<std::string> { "manifest-physics-material" });
  EXPECT_FALSE(oxygen::content::import::EffectiveContentHashingEnabled(
    request->options.with_content_hashing));
  ASSERT_TRUE(request->physics_material_descriptor.has_value());

  const auto normalized = json::parse(
    request->physics_material_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ground");
  EXPECT_EQ(normalized.at("virtual_path").get<std::string>(),
    "/.cooked/Physics/Materials/ground.opmat");
}

NOLINT_TEST(PhysicsMaterialDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const auto dir = MakeTempDir("schema_violation");
  const auto descriptor_path = dir / "Physics" / "bad.material.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "Bad",
      "static_friction": 0.9,
      "unexpected": true
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(
    errors.str().find("physics.material.descriptor.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(
  PhysicsMaterialDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const auto dir = MakeTempDir("missing_file");
  const auto descriptor_path = dir / "Physics" / "missing.material.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("failed to open physics material descriptor")
    != std::string::npos);
}

NOLINT_TEST(
  PhysicsMaterialDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const auto dir = MakeTempDir("relative_output");
  const auto descriptor_path = dir / "Physics" / "ok.material.json";
  WriteTextFile(descriptor_path, R"({ "name": "ground" })");

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("cooked root must be an absolute path")
    != std::string::npos);
}

} // namespace

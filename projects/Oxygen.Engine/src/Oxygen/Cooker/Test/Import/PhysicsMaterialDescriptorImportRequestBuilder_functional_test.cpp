//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/PhysicsMaterialDescriptorImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportSettings.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::PhysicsMaterialDescriptorImportSettings;
using oxygen::content::import::internal::BuildPhysicsMaterialDescriptorRequest;
using oxygen::cooker::test::kContentHashingDefault;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteText;

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
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "valid_request";
  const auto descriptor_path = dir / "Physics" / "ground.material.json";
  WriteText(descriptor_path,
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

  ASSERT_HAS_VALUE(request) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name,
    std::optional<std::string> { "manifest-physics-material" });
  // Release must hash authored content even when the descriptor opts out.
  EXPECT_EQ(request->options.with_content_hashing, kContentHashingDefault);
  ASSERT_HAS_VALUE(request->physics_material_descriptor);

  const auto normalized = json::parse(
    request->physics_material_descriptor->normalized_descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "ground");
  EXPECT_EQ(normalized.at("virtual_path").get<std::string>(),
    "/.cooked/Physics/Materials/ground.opmat");
}

NOLINT_TEST(PhysicsMaterialDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "schema_violation";
  const auto descriptor_path = dir / "Physics" / "bad.material.json";
  WriteText(descriptor_path,
    R"({
      "name": "Bad",
      "static_friction": 0.9,
      "unexpected": true
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr(
      "physics.material.descriptor.schema_validation_failed"));
}

NOLINT_TEST(
  PhysicsMaterialDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "missing_file";
  const auto descriptor_path = dir / "Physics" / "missing.material.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors.str(),
    ::testing::HasSubstr("failed to open physics material descriptor"));
}

NOLINT_TEST(
  PhysicsMaterialDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const ScopedTempDir temp;
  const auto dir = temp.Path() / "relative_output";
  const auto descriptor_path = dir / "Physics" / "ok.material.json";
  WriteText(descriptor_path, R"({ "name": "ground" })");

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsMaterialDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(
    errors.str(), ::testing::HasSubstr("cooked root must be an absolute path"));
}

} // namespace

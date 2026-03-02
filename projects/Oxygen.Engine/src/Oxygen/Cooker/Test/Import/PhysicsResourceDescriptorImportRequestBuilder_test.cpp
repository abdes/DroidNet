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

#include <Oxygen/Cooker/Import/PhysicsResourceDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsResourceDescriptorImportSettings.h>

namespace {

using nlohmann::json;
using oxygen::content::import::PhysicsResourceDescriptorImportSettings;
using oxygen::content::import::internal::BuildPhysicsResourceDescriptorRequest;

auto MakeTempDir(const std::string_view stem) -> std::filesystem::path
{
  auto dir
    = std::filesystem::temp_directory_path() / "oxygen_physics_resource_desc";
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
  -> PhysicsResourceDescriptorImportSettings
{
  auto settings = PhysicsResourceDescriptorImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-physics-resource";
  settings.with_content_hashing = true;
  return settings;
}

NOLINT_TEST(PhysicsResourceDescriptorImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorWithNormalizedPayload)
{
  const auto dir = MakeTempDir("valid_request");
  const auto descriptor_path = dir / "Physics" / "park_joint.resource.json";
  WriteTextFile(descriptor_path,
    R"({
      "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-resource-descriptor.schema.json",
      "name": "park_hinge_joint_a",
      "content_hashing": false,
      "source": "payloads/park_hinge_joint_a.jphbin",
      "format": "jolt_constraint_binary",
      "virtual_path": "/.cooked/Physics/Resources/park_hinge_joint_a.opres"
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsResourceDescriptorRequest(settings, errors);

  ASSERT_TRUE(request.has_value()) << errors.str();
  EXPECT_TRUE(errors.str().empty());
  ASSERT_TRUE(request->cooked_root.has_value());
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name,
    std::optional<std::string> { "manifest-physics-resource" });
  EXPECT_FALSE(oxygen::content::import::EffectiveContentHashingEnabled(
    request->options.with_content_hashing));
  ASSERT_TRUE(request->physics_resource_descriptor.has_value());

  const auto normalized = json::parse(
    request->physics_resource_descriptor->normalized_descriptor_json);
  EXPECT_EQ(
    normalized.at("format").get<std::string>(), "jolt_constraint_binary");
  EXPECT_EQ(normalized.at("virtual_path").get<std::string>(),
    "/.cooked/Physics/Resources/park_hinge_joint_a.opres");
}

NOLINT_TEST(PhysicsResourceDescriptorImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const auto dir = MakeTempDir("schema_violation");
  const auto descriptor_path = dir / "Physics" / "bad.resource.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "Bad",
      "source": "payload.bin",
      "format": "jolt_shape_binary",
      "unexpected": true
    })");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsResourceDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(
    errors.str().find("physics.resource.descriptor.schema_validation_failed")
    != std::string::npos);
}

NOLINT_TEST(
  PhysicsResourceDescriptorImportRequestBuilderTest, RejectsMissingFile)
{
  const auto dir = MakeTempDir("missing_file");
  const auto descriptor_path = dir / "Physics" / "missing.resource.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsResourceDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("failed to open physics resource descriptor")
    != std::string::npos);
}

NOLINT_TEST(
  PhysicsResourceDescriptorImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const auto dir = MakeTempDir("relative_output");
  const auto descriptor_path = dir / "Physics" / "ok.resource.json";
  WriteTextFile(descriptor_path,
    R"({
      "name": "JointA",
      "source": "payload.bin",
      "format": "jolt_shape_binary"
    })");

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::ostringstream {};

  const auto request = BuildPhysicsResourceDescriptorRequest(settings, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_TRUE(errors.str().find("cooked root must be an absolute path")
    != std::string::npos);
}

} // namespace
